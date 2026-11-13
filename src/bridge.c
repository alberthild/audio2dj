// Experimental Audio 2 DJ PCM bridge.
// Protocol derived from Linux sound/usb/caiaq (GPL-2.0-or-later).
// Original caiaq driver: Copyright (c) 2006-2008 Daniel Mack, Karsten Wiese.
// macOS bridge modifications: Copyright (c) 2026 Albert Hild.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <libusb.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <pthread/qos.h>
#define COUNT 32
#define PACKETS 32
#define SIZE 512
struct slot { struct libusb_transfer *input, *output; unsigned char in[PACKETS*SIZE], out[PACKETS*SIZE]; int reading, writing; };
static struct slot slots[COUNT];
static volatile sig_atomic_t running = 1;
static int failures;
#define RING 16384
static float ring[RING][4];
static unsigned long written, consumed, underruns, overruns;
static double fraction;
static double peak;
static unsigned long clipped, nonfinite;
static int primed, udp=-1;
static double last_audio;
static double monotonic_seconds(void) {
    struct timespec value; clock_gettime(CLOCK_MONOTONIC,&value);
    return value.tv_sec+value.tv_nsec/1e9;
}
static unsigned int cursor=4;
static unsigned char pcm[12];
static void stop_requested(int signal_number) { (void)signal_number; running=0; }
static void receive_audio(void) {
    float packet[256*4];
    for(int count=0;count<32;count++) {
        ssize_t size=recv(udp,packet,sizeof(packet),MSG_DONTWAIT);
        if(size<0) break;
        if(size<=0 || size%16) continue;
        last_audio=monotonic_seconds();
        unsigned int frames=(unsigned int)size/16;
        if(written-consumed+frames>=RING) { overruns++; continue; }
        for(unsigned int i=0;i<frames;i++) memcpy(ring[(written+i)%RING],packet+i*4,16);
        written+=frames;
    }
}
static void next_frame(void) {
    float values[4]={0};
    unsigned long available=written-consumed;
    if(!primed && available>=1024) primed=1;
    if(primed && available<2) { primed=0; underruns++; }
    if(primed) {
        double correction=((double)available-1024.0)*0.000002;
        if(correction>0.002) correction=0.002;
        if(correction< -0.002) correction= -0.002;
        for(int channel=0;channel<4;channel++) {
            float first=ring[consumed%RING][channel];
            float second=ring[(consumed+1)%RING][channel];
            values[channel]=(float)(first+(second-first)*fraction);
        }
        fraction+=1.0+correction;
        unsigned int advance=(unsigned int)fraction;
        fraction-=advance;
        consumed+=advance;
    }
    for(int channel=0;channel<4;channel++) {
        double value=isfinite(values[channel]) ? values[channel] : 0;
        if(!isfinite(values[channel])) nonfinite++;
        if(fabs(value)>peak) peak=fabs(value);
        if(fabs(value)>1) clipped++;
        if(value>1) value=1;
        if(value< -1) value= -1;
        unsigned int sample=(unsigned int)(int)(value*8388607.0)&0xffffff;
        for(int byte=0;byte<3;byte++) pcm[channel*3+byte]=(unsigned char)(sample>>(16-8*byte));
    }
}
static unsigned long received, sent, bytes, dropped, device_errors;
static unsigned long input_lengths[513];
static unsigned int feedback[1024];
static unsigned long feedback_written, feedback_read, feedback_fallbacks;
static unsigned long hardware_frames, output_frames;
static double last_output, max_output_gap;
static void queue_output(struct slot *slot) {
    receive_audio();
    unsigned int offset=0, previous_frames=0;
    unsigned int total_frames=PACKETS*6;
    if(feedback_written>feedback_read) total_frames=feedback[feedback_read++%1024];
    else feedback_fallbacks++;
    output_frames+=total_frames;
    for (int p=0;p<PACKETS;p++) {
        unsigned int cumulative_frames=total_frames*(p+1)/PACKETS;
        unsigned int length=(cumulative_frames-previous_frames)*16;
        previous_frames=cumulative_frames;
        slot->output->iso_packet_desc[p].length=length;
        memset(slot->out+offset,0,length);
        for(unsigned int position=0;position<length;) {
            for(int stream=0;stream<2;stream++,position++) slot->out[offset+position]=pcm[stream*6+cursor];
            cursor++;
            if(cursor==6) { cursor=0; next_frame(); }
            if(position%8==4) position+=2;
        }
        for(unsigned int i=4;i<length;i+=8) {
            slot->out[offset+i]=(unsigned char)(~(i/8)&1);
            slot->out[offset+i+1]=(unsigned char)(2|(~((i+1)/8)&1));
        }
        offset+=length;
    }
    slot->output->length=(int)offset;
    int result=libusb_submit_transfer(slot->output);
    if(result) { fprintf(stderr,"Output submit: %s\n",libusb_error_name(result)); failures++; running=0; }
    else { slot->writing=1; bytes+=offset; }
}
static void output_done(struct libusb_transfer *transfer) {
    struct slot *slot=transfer->user_data;
    slot->writing=0;
    if(!running || transfer->status==LIBUSB_TRANSFER_CANCELLED) return;
    double now=monotonic_seconds();
    if(last_output && now-last_output>max_output_gap) max_output_gap=now-last_output;
    last_output=now;
    if(transfer->status!=LIBUSB_TRANSFER_COMPLETED) {
        fprintf(stderr,"Output completion status %d\n",transfer->status);
        failures++; running=0; return;
    }
    for(int i=0;i<PACKETS;i++) if(transfer->iso_packet_desc[i].status) failures++;
    sent++;
    queue_output(slot);
}
static void input_done(struct libusb_transfer *transfer) {
    struct slot *slot=transfer->user_data;
    slot->reading=0;
    if(!running) return;
    if(transfer->status!=LIBUSB_TRANSFER_COMPLETED) { failures++; running=0; return; }
    received++;
    unsigned int total_bytes=0;
    int valid_feedback=1;
    for(int p=0;p<PACKETS;p++) {
        if(transfer->iso_packet_desc[p].status) { failures++; valid_feedback=0; continue; }
        total_bytes+=transfer->iso_packet_desc[p].actual_length;
        if(transfer->iso_packet_desc[p].actual_length<=512) input_lengths[transfer->iso_packet_desc[p].actual_length]++;
        unsigned char *data=libusb_get_iso_packet_buffer_simple(transfer,p);
        for(unsigned int i=0;i+1<transfer->iso_packet_desc[p].actual_length;i+=8)
            if((data[i]|data[i+1])&0x80) device_errors++;
    }
    if(valid_feedback && total_bytes%16==0 && total_bytes>=PACKETS*16 && total_bytes<=PACKETS*SIZE && feedback_written-feedback_read<1024) {
        feedback[feedback_written++%1024]=total_bytes/16;
        hardware_frames+=total_bytes/16;
    }
    int result=libusb_submit_transfer(slot->input);
    if(result) { failures++; running=0; } else slot->reading=1;
}
int main(void) {
    int qos_result=pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
    fprintf(stderr,"Audio thread QoS: %d\n",qos_result);
    signal(SIGINT,stop_requested);
    signal(SIGTERM,stop_requested);
    udp=socket(AF_INET,SOCK_DGRAM,0);
    int receive_buffer=1024*1024;
    setsockopt(udp,SOL_SOCKET,SO_RCVBUF,&receive_buffer,sizeof(receive_buffer));
    struct sockaddr_in address={0};
    address.sin_family=AF_INET; address.sin_port=htons(49172); address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(udp<0 || bind(udp,(struct sockaddr*)&address,sizeof(address))) { perror("audio receiver"); return 1; }
    setvbuf(stdout,NULL,_IOLBF,0);
    puts("Waiting for CoreAudio playback on local port 49172.");
    struct pollfd incoming={udp,POLLIN,0};
    while(running) {
        int ready=poll(&incoming,1,1000);
        if(ready>0) break;
        if(ready<0 && errno!=EINTR) { close(udp); return 1; }
    }
    if(!running) { close(udp); return 0; }
    last_audio=monotonic_seconds();
    libusb_context *context=NULL;
    libusb_device_handle *device=NULL;
    int result=libusb_init(&context), claimed=0;
    if(result) return 1;
    device=libusb_open_device_with_vid_pid(context,0x17cc,0x041c);
    if(!device) { result=LIBUSB_ERROR_NO_DEVICE; goto finish; }
    result=libusb_reset_device(device);
    if(result) goto finish;
    result=libusb_claim_interface(device,0);
    if(result) goto finish;
    claimed=1;
    result=libusb_set_interface_alt_setting(device,0,1);
    if(result) goto finish;
    unsigned char params[]={9,1,2,176,0,1}, reply[64];
    int length=0;
    result=libusb_bulk_transfer(device,1,params,sizeof(params),&length,1000);
    if(result) goto finish;
    result=libusb_bulk_transfer(device,0x81,reply,sizeof(reply),&length,1000);
    if(result) goto finish;
    if(length<2 || reply[0]!=9 || reply[1]!=1) { result=LIBUSB_ERROR_OTHER; goto finish; }
    puts("48 kHz / 24 bit accepted. Experimental PCM bridge listening on 127.0.0.1:49172.");
    for(int i=0;i<COUNT;i++) {
        struct slot *slot=&slots[i];
        slot->input=libusb_alloc_transfer(PACKETS);
        slot->output=libusb_alloc_transfer(PACKETS);
        if(!slot->input || !slot->output) { result=LIBUSB_ERROR_NO_MEM; running=0; break; }
        libusb_fill_iso_transfer(slot->input,device,0x82,slot->in,sizeof(slot->in),PACKETS,input_done,slot,1000);
        libusb_set_iso_packet_lengths(slot->input,SIZE);
        libusb_fill_iso_transfer(slot->output,device,0x06,slot->out,sizeof(slot->out),PACKETS,output_done,slot,1000);
        result=libusb_submit_transfer(slot->input);
        if(result) { running=0; break; }
        slot->reading=1;
        queue_output(slot);
        if(!running) break;
    }
    double last_report=monotonic_seconds();
    while(running) {
        double now=monotonic_seconds();
        if(now-last_report>=5.0) {
            printf("Live: out=%lu errors=%d device=%lu underruns=%lu overruns=%lu buffered=%lu max_callback_gap_ms=%.2f\n",sent,failures,device_errors,underruns,overruns,written-consumed,max_output_gap*1000);
            printf("PCM peak=%.6f clipped=%lu nonfinite=%lu gain=1.0\n",peak,clipped,nonfinite);
            printf("Clock: hardware_frames=%lu sent_frames=%lu feedback_pending=%lu startup_fallbacks=%lu\n",hardware_frames,output_frames,feedback_written-feedback_read,feedback_fallbacks);
            printf("Input lengths:");
            for(int n=0;n<=512;n++) if(input_lengths[n]) printf(" %d:%lu",n,input_lengths[n]);
            puts("");
            last_report=now;
        }
        if(now-last_audio>2.0) break;
        struct timeval timeout={0,100000};
        result=libusb_handle_events_timeout(context,&timeout);
        if(result==LIBUSB_ERROR_INTERRUPTED) { result=0; continue; }
        if(result) break;
    }
    running=0;
    for(int i=0;i<COUNT;i++) {
        if(slots[i].reading) libusb_cancel_transfer(slots[i].input);
        if(slots[i].writing) libusb_cancel_transfer(slots[i].output);
    }
    for(;;) {
        int pending=0;
        for(int i=0;i<COUNT;i++) pending+=slots[i].reading+slots[i].writing;
        if(!pending) break;
        struct timeval timeout={0,100000};
        libusb_handle_events_timeout(context,&timeout);
    }
    for(int i=0;i<COUNT;i++) { libusb_free_transfer(slots[i].input); libusb_free_transfer(slots[i].output); }
    printf("Input transfers %lu, output transfers %lu, bytes %lu, busy slots %lu, errors %d\n",received,sent,bytes,dropped,failures);
    printf("Device output error flags: %lu\n",device_errors);
    printf("Underruns %lu, overruns %lu, queued frames %lu, received PCM frames %lu\n",underruns,overruns,written-consumed,written);
    if(!received || !sent || failures || dropped || device_errors) result=LIBUSB_ERROR_IO;
finish:
    if(result) fprintf(stderr,"Result: %s\n",libusb_error_name(result));
    if(claimed) { libusb_set_interface_alt_setting(device,0,0); libusb_release_interface(device,0); }
    if(device) libusb_close(device);
    libusb_exit(context);
    close(udp);
    return result ? 1 : 0;
}
