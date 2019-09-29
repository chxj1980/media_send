#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "media_send.h"

void init_signals(void)
{
    struct sigaction sa;

    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    /* 忽略socket写入错误导致的SIGPIPE信号 */
    sigaddset(&sa.sa_mask, SIGPIPE);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

int main(int argc, char *argv[])
{
    init_signals();
    
    //打开H264视频文件
    FILE *file=fopen(H264_FILE,"r");
    if(NULL == file)
    {
        printf("open file failed..\n");
        return -1;
    }
    struct stat statbuf;
    int    start_send = 0;
    //获取文件长度
    stat(H264_FILE,&statbuf);
    int file_size_cur=0;
    int file_size=statbuf.st_size;
    unsigned int file_index = 0;
    packet_info pPacker = {0};
    frame_info frame = {0};
    pPacker.s64CurPts = 853945250;
    
    int sock_fd = sock_udp_open(SOCK_DGRAM);
    if (sock_fd <= 0)
    {
        printf("sock_open failed\n");
        return NULL;
    }
    unsigned char *buf=(unsigned char *)calloc(1,MAX_FILE_LEN);
    if(buf == NULL)
    {
        return NULL;
    }
    int first_idr = 1;
    char *send_buf = (char *)calloc(1,MAX_FILE_LEN);
    int send_len = 0;
    int frameRate = 25;
    while(1)
    {
        pPacker.sock_fd = sock_fd;
        strcpy(pPacker.recv_ip, "172.24.11.154");
        pPacker.recv_port = 9878;
        fseek(file,file_index,0);
        memset(buf,0,MAX_FILE_LEN);
        int read_len = fread(buf,1,MAX_FILE_LEN,file);
        if(read_len <= 0)
        {
            file_size_cur = 0;
            break;
        }
        //printf("1#file_index:%d read_len:%d file_size_cur:%d\n", file_index, read_len, file_size_cur);
        if (file_size_cur + read_len >= file_size)
        {
            printf("file end...\n");
            file_index=0;
            file_size_cur = 0;
            fseek(file,file_index,0);
            continue;
        }
        memset(&frame,0,sizeof(frame));
        if(0 == get_h264_frame(buf, &frame))
        {
            file_size_cur += frame.len;
            file_index += frame.len;
            switch (frame.type)
            {
                case NAL_TYPE_SPS:
                {
                    printf("NAL_TYPE_SPS\n");
                    memcpy(send_buf + send_len, buf, frame.len);
                    send_len += frame.len;
                    sps_t sps = {0};
                    //offset 00 00 00 01 27 ...[data]
                    int offset = frame.start_code_len +1;
                    parse_h264_sps(buf + offset, frame.len - offset, &sps);
                    //printf("b_fixed_frame_rate:%d\n", sps.vui.b_fixed_frame_rate);
                    //printf("i_time_scale:%d\n", sps.vui.i_time_scale);
                    //printf("i_num_units_in_tick:%d\n", sps.vui.i_num_units_in_tick);
                    if (sps.vui.b_fixed_frame_rate)
                    {
                        frameRate = sps.vui.i_time_scale/(2 * sps.vui.i_num_units_in_tick);
                    }
                    else
                    {
                        frameRate = sps.vui.i_time_scale/sps.vui.i_num_units_in_tick;
                    }                        
                    printf("frameRate:%d width*height:%d*%d\n", frameRate,(sps.pic_width_in_mbs_minus1 + 1)*16,
                                (sps.pic_height_in_map_units_minus1 + 1)*16);
                    break;
                }
                case NAL_TYPE_PPS:
                {
                    printf("NAL_TYPE_PPS\n");
                    pps_t pps = {0};
                    //offset 00 00 00 01 28 ...[data]
                    int offset = frame.start_code_len +1;
                    parse_h264_pps(buf + offset, frame.len - offset, &pps);
                    memcpy(send_buf + send_len, buf, frame.len);
                    send_len += frame.len;
                    break;
                }
                case NAL_TYPE_DELIMITER:
                {
                    //printf("NAL_TYPE_PPS\n");
                    memcpy(send_buf + send_len, buf, frame.len);
                    send_len += frame.len;
                    break;
                }
                case NAL_TYPE_IDR:
                {
                    //printf("NAL_TYPE_IDR\n");
                    memcpy(send_buf + send_len, buf, frame.len);
                    send_len += frame.len;
                    pPacker.IFrame = 1;
                    //帧率为25fps=3600 30fps=3000
                    pPacker.s64CurPts += 90*1000/frameRate;
                    pack_ps_stream(send_buf, send_len, &pPacker, 0);
                    memset(send_buf, 0, send_len);
                    send_len = 0;
                    usleep(1000*1000/frameRate - 10*1000);
                    break;
                }
                case NAL_TYPE_NOTIDR:
                {
                    //printf("NAL_TYPE_NOTIDR\n");
                    memcpy(send_buf + send_len, buf, frame.len);
                    send_len += frame.len;
                    pPacker.IFrame = 0;
                    //clock:90kHz 25fps增量3600 30fps增量3000
                    pPacker.s64CurPts += 90*1000/frameRate;
                    pack_ps_stream(send_buf, send_len, &pPacker, 0);
                    memset(send_buf, 0, send_len);
                    send_len = 0;
                    usleep(1000*1000/frameRate - 10*1000);
                    break;
                }
            }
        
       }
       else
       {
           usleep(1000*1000);
       }
    }
    free(buf);
    fclose(file);
    return 0;
}