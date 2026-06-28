#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>
#include <vector>
#include <stdint.h>
#include <unistd.h>

#include "rk_aiq_user_api2_sysctl.h"
#include "rk_aiq_user_api2_ae.h"
#include "ae/rk_aiq_uapi_ae_int_types_v2.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "luckfox_mpi.h"
#include <sys/uio.h>

#define RTP_SSRC        0x12345678
#define RTP_PT_H264     96

// ==========================================
// 1. 定义统一参数结构体，实现参数化配置
// ==========================================
struct AppConfig {
    char dest_ip[64];       // 目标接收端IP
    int dest_port;          // 目标端口
    int width;              // 视频分辨率宽度
    int height;             // 视频分辨率高度
    int bitrate_kbps;       // 编码码率（单位: kbps）
    int gop;                // 帧间隔 GOP
    char sensor_name[128];  // 摄像头 Sensor 节点名
    float exposure_time;    // 手动曝光时间 (<=0 代表启用自动曝光模式)
    float gain_val;         // 手动增益值
};

static rk_aiq_sys_ctx_t *g_aiq_ctx = NULL;
volatile bool g_quit = false;

void sig_handler(int sig) {
    g_quit = true;
}

struct RtpSender {
    int fd;
    sockaddr_in addr;
    uint16_t seq;
    uint32_t ssrc;
};

// ==========================================
// 2. 动态调节曝光与增益（支持自动与手动一键切换）
// ==========================================
static void set_exposure_params(const rk_aiq_sys_ctx_t *ctx, float time_val, float gain_val) {
    if (!ctx) return;
    
    Uapi_ExpSwAttrV2_t expAttr;
    memset(&expAttr, 0, sizeof(expAttr));
    if (rk_aiq_user_api2_ae_getExpSwAttr(ctx, &expAttr) != 0) return;

    if (time_val <= 0.0f) {
        // 曝光时间为 0 或负数时，强制启用自动曝光 (AUTO) 模式
        expAttr.AecOpType = RK_AIQ_OP_MODE_AUTO;
        rk_aiq_user_api2_ae_setExpSwAttr(ctx, expAttr);
        printf("[ISP] AE mode set to AUTO (Camera auto exposure active)\n");
    } else {
        // 手动曝光模式
        expAttr.AecOpType = RK_AIQ_OP_MODE_MANUAL;
        expAttr.stManual.LinearAE.ManualTimeEn = true;
        expAttr.stManual.LinearAE.ManualGainEn = true;
        expAttr.stManual.LinearAE.ManualIspDgainEn = false;
        expAttr.stManual.LinearAE.TimeValue = time_val;   
        expAttr.stManual.LinearAE.GainValue = gain_val;     
        expAttr.stManual.LinearAE.IspDGainValue = 1.0f; 

        rk_aiq_user_api2_ae_setExpSwAttr(ctx, expAttr);
        printf("[ISP] AE mode set to MANUAL: Time=%fs, Gain=%f\n", time_val, gain_val);
    }
}

// ==========================================
// 3. 支持动态 IP 与 端口的 RTP 初始化
// ==========================================
static int rtp_init(RtpSender *s, const char *ip, int port) {
    s->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->fd < 0) return -1;

    int flags = fcntl(s->fd, F_GETFL, 0);
    fcntl(s->fd, F_SETFL, flags | O_NONBLOCK);

    int sndbuf_size = 2 * 1024 * 1024;
    setsockopt(s->fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size));

    memset(&s->addr, 0, sizeof(s->addr));
    s->addr.sin_family = AF_INET;
    s->addr.sin_port = htons(port);
    s->addr.sin_addr.s_addr = inet_addr(ip);

    s->seq = 0;
    s->ssrc = RTP_SSRC;
    return 0;
}

static int rtp_send_iovec(RtpSender *s, struct iovec *iov, int iovcnt) {
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &s->addr;
    msg.msg_namelen = sizeof(s->addr);
    msg.msg_iov = iov;
    msg.msg_iovlen = iovcnt;

    ssize_t ret = sendmsg(s->fd, &msg, MSG_DONTWAIT);
    s->seq++;
    return 0;
}

static void build_rtp_header(RtpSender *s, uint8_t *header, uint32_t timestamp, bool marker, uint8_t payload_type) {
    header[0] = 0x80;
    header[1] = (marker ? 0x80 : 0x00) | payload_type;
    header[2] = (s->seq >> 8) & 0xFF;
    header[3] = (s->seq >> 0) & 0xFF;
    header[4] = (timestamp >> 24) & 0xFF;
    header[5] = (timestamp >> 16) & 0xFF;
    header[6] = (timestamp >> 8) & 0xFF;
    header[7] = (timestamp >> 0) & 0xFF;
    header[8] = (s->ssrc >> 24) & 0xFF;
    header[9] = (s->ssrc >> 16) & 0xFF;
    header[10] = (s->ssrc >> 8) & 0xFF;
    header[11] = (s->ssrc >> 0) & 0xFF;
}

static int rtp_send_h264_nal(RtpSender *s, const uint8_t *nal, size_t nal_len, uint32_t ts, bool is_last_nal) {
    if (nal_len == 0) return 0;
    const size_t max_payload = 1400; 
    
    if (nal_len <= max_payload) {
        uint8_t header[12];
        build_rtp_header(s, header, ts, is_last_nal, RTP_PT_H264);
        struct iovec iov[2];
        iov[0].iov_base = header;
        iov[0].iov_len = 12;
        iov[1].iov_base = (void*)nal;
        iov[1].iov_len = nal_len;
        return rtp_send_iovec(s, iov, 2);
    }
    
    uint8_t nal_hdr = nal[0];
    uint8_t fu_indicator = (nal_hdr & 0x60) | 28;
    uint8_t nal_type = nal_hdr & 0x1F;

    size_t pos = 1;
    while (pos < nal_len) {
        size_t chunk = (nal_len - pos > max_payload) ? max_payload : (nal_len - pos);
        bool bStart = (pos == 1);
        bool bEnd = (pos + chunk == nal_len);

        uint8_t header[14];
        build_rtp_header(s, header, ts, bEnd ? is_last_nal : false, RTP_PT_H264);
        header[12] = fu_indicator;
        header[13] = (bStart ? 0x80 : 0x00) | (bEnd ? 0x40 : 0x00) | nal_type;

        struct iovec iov[2];
        iov[0].iov_base = header;
        iov[0].iov_len = 14;
        iov[1].iov_base = (void*)(nal + pos);
        iov[1].iov_len = chunk;

        rtp_send_iovec(s, iov, 2);
        pos += chunk;
    }
    return 0;
}

static void rtp_send_h264_nal_stream(RtpSender *s, const uint8_t *data, size_t len, uint32_t ts, bool is_frame_last_pack) {
    size_t i = 0;
    while (i < len) {
        if (i + 2 >= len) break;
        int sc_len = 0;
        if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 1) sc_len = 3;
        else if (i + 3 < len && data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1) sc_len = 4;

        if (sc_len > 0) {
            size_t nal_start = i + sc_len;
            size_t next_sc = nal_start;
            while (next_sc < len) {
                if (next_sc + 2 < len && data[next_sc] == 0 && data[next_sc+1] == 0 && data[next_sc+2] == 1) {
                    if (next_sc > 0 && data[next_sc-1] == 0) next_sc--; 
                    break;
                }
                next_sc++;
            }
            size_t nal_len = next_sc - nal_start;
            bool is_nal_last = (next_sc == len) && is_frame_last_pack;

            if (is_nal_last) {
                while (nal_len > 0 && data[nal_start + nal_len - 1] == 0x00) {
                    nal_len--;
                }
            }

            rtp_send_h264_nal(s, data + nal_start, nal_len, ts, is_nal_last);
            i = next_sc; 
        } else {
            i++; 
        }
    }
}

// 帮助打印信息
static void print_usage(const char *prog_name, const AppConfig &def) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -i <ip>       Destination RTP IP (default: %s)\n", def.dest_ip);
    printf("  -p <port>     Destination RTP Port (default: %d)\n", def.dest_port);
    printf("  -w <width>    Video resolution width (default: %d)\n", def.width);
    printf("  -h <height>   Video resolution height (default: %d)\n", def.height);
    printf("  -b <bitrate>  VENC Bitrate in kbps (default: %d kbps)\n", def.bitrate_kbps);
    printf("  -g <gop>      VENC GOP size (default: %d)\n", def.gop);
    printf("  -s <sensor>   Sensor device entity name (default: %s)\n", def.sensor_name);
    printf("  -e <time>     Manual exposure time (default: %f, set <= 0 for AUTO exposure)\n", def.exposure_time);
    printf("  -t <gain>     Manual gain value (default: %f)\n", def.gain_val);
}

// ==========================================
// 4. 解析命令行参数并应用
// ==========================================
int main(int argc, char *argv[]) {
    // 默认配置初始化
    AppConfig config;
    strcpy(config.dest_ip, "192.168.50.2");
    config.dest_port = 5004;
    config.width = 1280;
    config.height = 720;
    config.bitrate_kbps = 2048; // 2Mbps
    config.gop = 30;
    strcpy(config.sensor_name, "m00_b_sc3336 4-0030");
    config.exposure_time = 0.008f; // 手动曝光 8ms
    config.gain_val = 1.5f;

    int opt;
    while ((opt = getopt(argc, argv, "i:p:w:h:b:g:s:e:t:?")) != -1) {
        switch (opt) {
            case 'i': strncpy(config.dest_ip, optarg, sizeof(config.dest_ip) - 1); break;
            case 'p': config.dest_port = atoi(optarg); break;
            case 'w': config.width = atoi(optarg); break;
            case 'h': config.height = atoi(optarg); break;
            case 'b': config.bitrate_kbps = atoi(optarg); break;
            case 'g': config.gop = atoi(optarg); break;
            case 's': strncpy(config.sensor_name, optarg, sizeof(config.sensor_name) - 1); break;
            case 'e': config.exposure_time = atof(optarg); break;
            case 't': config.gain_val = atof(optarg); break;
            case '?':
            default:
                print_usage(argv[0], config);
                return 0;
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("[System] Init Camera Stream: %dx%d, Bitrate=%d kbps, SENSOR=%s\n", 
           config.width, config.height, config.bitrate_kbps, config.sensor_name);

    RK_S32 s32Ret = 0;
    const char *iq_dir = "/etc/iqfiles";
    rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;

    // AIQ init
    g_aiq_ctx = rk_aiq_uapi2_sysctl_init(config.sensor_name, iq_dir, NULL, NULL);
    if (!g_aiq_ctx) {
        printf("rk_aiq_uapi2_sysctl_init failed for sensor: %s\n", config.sensor_name);
        return -1;
    }

    if (rk_aiq_uapi2_sysctl_prepare(g_aiq_ctx, config.width, config.height, hdr_mode) != 0) return -1;
    if (rk_aiq_uapi2_sysctl_start(g_aiq_ctx) != 0) return -1;

    printf("[System] AIQ Engine started successfully.\n");
    
    // 应用动态曝光配置
    set_exposure_params(g_aiq_ctx, config.exposure_time, config.gain_val);

    if (RK_MPI_SYS_Init() != RK_SUCCESS) return -1;

    // VI & VENC init (传入动态宽、高、码率和GOP)
    vi_dev_init();
    vi_chn_init(0, config.width, config.height);
    venc_init(0, config.width, config.height, RK_VIDEO_ID_AVC, config.bitrate_kbps, config.gop);

    MPP_CHN_S stSrcChn, stDestChn;
    stSrcChn.enModId = RK_ID_VI;    stSrcChn.s32DevId = 0;  stSrcChn.s32ChnId = 0;
    stDestChn.enModId = RK_ID_VENC; stDestChn.s32DevId = 0; stDestChn.s32ChnId = 0;
    if (RK_MPI_SYS_Bind(&stSrcChn, &stDestChn) != RK_SUCCESS) {
        printf("ERROR: Bind VI to VENC failed!\n");
        return -1;
    }
    printf("Hardware Bind VI to VENC Success!\n");

    RtpSender rtp_sender;
    if (rtp_init(&rtp_sender, config.dest_ip, config.dest_port) < 0) return -1;
    printf("RTP sender init success -> %s:%d\n", config.dest_ip, config.dest_port);

    VENC_STREAM_S stFrame;
    memset(&stFrame, 0, sizeof(stFrame));
    stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S) * 32); 

    while (!g_quit) {
        s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, 1000);
        
        if (s32Ret == RK_SUCCESS) {
            uint64_t current_hw_pts = stFrame.pstPack[0].u64PTS;
            uint32_t rtp_ts = (uint32_t)(current_hw_pts * 90 / 1000);

            for (RK_U32 i = 0; i < stFrame.u32PackCount; i++) {
                uint32_t actual_len = stFrame.pstPack[i].u32Len;
                void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack[i].pMbBlk);
                if (pData == NULL || actual_len == 0) continue; 
                
                uint8_t *nal_data = (uint8_t *)pData;
                bool is_last_pack = (i == (stFrame.u32PackCount - 1));
                rtp_send_h264_nal_stream(&rtp_sender, nal_data, actual_len, rtp_ts, is_last_pack);
            }
            RK_MPI_VENC_ReleaseStream(0, &stFrame);
        } else {
            usleep(1000); 
        }
    }

    // ==============================================================
    // 真正的终极安全释放序列：修复 AXI 总线死锁
    // ==============================================================
    printf("\n[!!!] Caught exit signal! Initiating Safe Graceful Exit...\n");

    if (g_aiq_ctx) {
        printf("[!!!] Stopping AIQ (ISP) hardware DMA first...\n");
        rk_aiq_uapi2_sysctl_stop(g_aiq_ctx, false);
        rk_aiq_uapi2_sysctl_deinit(g_aiq_ctx);
        g_aiq_ctx = NULL;
    }

    RK_MPI_VENC_StopRecvFrame(0);

    printf("[!!!] Draining network cache naturally (1500ms)...\n");
    usleep(1500000); 

    close(rtp_sender.fd);
    usleep(200000); 

    printf("[OK ] Hardware DMA stopped and Network is quiet.\n");

    RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    RK_MPI_VI_DisableChn(0, 0);
    RK_MPI_VI_DisableDev(0);
    RK_MPI_VENC_DestroyChn(0);
    free(stFrame.pstPack);
    RK_MPI_SYS_Exit();

    printf("Graceful exit finished safely!\n");
    return 0;
}