#!/usr/bin/env python3
"""Deterministically exercise the real darwin_exit body and bridge cleanup guard.
No audio device is opened or reset by these tests.
"""
from pathlib import Path
import subprocess, tarfile, tempfile
r=Path(__file__).resolve().parents[1]
lib=r/'build/libusb-1.0.29'

def function(text):
    start=text.index('static void darwin_exit (')
    end=text.index('\nstatic int get_configuration_index', start)
    return text[start:end]

harness=r'''
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#define UNUSED(x) (void)(x)
#define usbi_mutex_lock pthread_mutex_lock
#define usbi_mutex_unlock pthread_mutex_unlock
struct libusb_context { int unused; };
static pthread_mutex_t darwin_cached_devices_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t libusb_darwin_at_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t libusb_darwin_at_cond=PTHREAD_COND_INITIALIZER;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond=PTHREAD_COND_INITIALIZER;
static pthread_t libusb_darwin_at;
static bool libusb_darwin_at_started, signalled;
static int init_count=2, cleaned;
static void *libusb_darwin_acfl=(void*)1, *libusb_darwin_acfls=(void*)1;
static void CFRunLoopSourceSignal(void *unused) {
 (void)unused; pthread_mutex_lock(&gate); signalled=true;
 pthread_cond_broadcast(&gate_cond); pthread_mutex_unlock(&gate);
}
static void CFRunLoopWakeUp(void *unused) { (void)unused; }
static void darwin_cleanup_devices(void) {
 // Cache cleanup must still be serialized after the event thread has exited.
 assert(pthread_mutex_trylock(&darwin_cached_devices_mutex)!=0);
 assert(!libusb_darwin_at_started); cleaned++;
}
static void *hotplug(void *unused) {
 (void)unused;
 pthread_mutex_lock(&gate);
 while(!signalled) pthread_cond_wait(&gate_cond,&gate);
 pthread_mutex_unlock(&gate);
 // Reproduce a hotplug callback acquiring the cache lock during shutdown.
 pthread_mutex_lock(&darwin_cached_devices_mutex);
 pthread_mutex_unlock(&darwin_cached_devices_mutex);
 pthread_mutex_lock(&libusb_darwin_at_mutex);
 libusb_darwin_acfl=NULL;
 pthread_cond_broadcast(&libusb_darwin_at_cond);
 pthread_mutex_unlock(&libusb_darwin_at_mutex);
 return NULL;
}
'''
main=r'''
int main(void) {
 darwin_exit(NULL); // A non-last context must leave the event loop alone.
 assert(init_count==1 && cleaned==0 && !signalled);
 assert(pthread_create(&libusb_darwin_at,NULL,hotplug,NULL)==0);
 libusb_darwin_at_started=true;
 darwin_exit(NULL);
 assert(init_count==0 && cleaned==1 && !libusb_darwin_at_started);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as folder:
    tmp=Path(folder)
    with tarfile.open(r/'vendor/libusb-1.0.29.tar.bz2') as archive:
        original=archive.extractfile('libusb-1.0.29/libusb/os/darwin_usb.c').read().decode()
    fixed=(lib/'libusb/os/darwin_usb.c').read_text()
    for label,source in [('old',original),('fixed',fixed)]:
        code=tmp/f'{label}.c';binary=tmp/label
        code.write_text(harness+function(source)+main)
        subprocess.run(['clang','-Wall','-Wextra','-pthread',str(code),'-o',str(binary)],check=True)
        if label=='old':
            try: subprocess.run([str(binary)],timeout=2,check=True)
            except subprocess.TimeoutExpired: print('PASS: unpatched shutdown deterministically deadlocks')
            else: raise AssertionError('Regression did not reproduce the original deadlock')
        else:
            for _ in range(50):subprocess.run([str(binary)],timeout=2,check=True)
            print('PASS: patched shutdown and non-last-context path (50 iterations)')
    # Include the bridge itself so the watchdog test uses the production handler.
    code=tmp/'guard.c';binary=tmp/'guard'
    code.write_text('#define main bridge_main\n#include "'+str(r/'src/bridge.c')+'"\n#undef main\n'
      'int main(int argc,char **argv) { (void)argv; signal(SIGALRM,cleanup_timeout); begin_cleanup();'
      'if(argc>1) { alarm(0); return 0; } for(;;) pause(); }\n')
    subprocess.run(['clang','-O2',str(code),'-I'+str(lib/'libusb'),str(lib/'libusb/.libs/libusb-1.0.a'),
        '-framework','IOKit','-framework','CoreFoundation','-framework','Security','-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=8)
    assert result.returncode==1 and 'cleanup exceeded 5 seconds' in result.stderr
    subprocess.run([str(binary),'normal'],timeout=2,check=True)
    print('PASS: stalled cleanup exits with failure; normal cleanup cancels deadline')
    code=tmp/'lifecycle.c';binary=tmp/'lifecycle'
    code.write_text('#include <libusb.h>\nint main(void) { for(int i=0;i<100;i++) {'
      'libusb_context *c=0; if(libusb_init(&c)) return 1; libusb_device **d=0;'
      'if(libusb_get_device_list(c,&d)<0) return 2; libusb_free_device_list(d,1); libusb_exit(c); } return 0; }')
    subprocess.run(['clang',str(code),'-I'+str(lib/'libusb'),str(lib/'libusb/.libs/libusb-1.0.a'),
        '-framework','IOKit','-framework','CoreFoundation','-framework','Security','-o',str(binary)],check=True)
    subprocess.run([str(binary)],timeout=45,check=True)
    print('PASS: 100 real libusb init/enumerate/exit cycles without resetting hardware')
