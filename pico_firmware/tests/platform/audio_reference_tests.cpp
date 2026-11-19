// Model of production worker logic; fake driver releases buffers immediately.
// It does not validate Zephyr threads, DMA ownership/latency or physical output.
#include "platform/pico_audio.cpp"
#include <cstdlib>
#include <iostream>
#include <functional>
namespace {
const device speaker{};
std::int64_t fake_now;
unsigned writes, drops, drains, failures;
std::function<void()> after_write;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << __LINE__ << ": " #x "\n"; } } while(false)
}
const device *fake_display_device() { return &speaker; }
bool device_is_ready(const device *) { return true; }
std::int64_t k_uptime_get() { return fake_now; }
void k_msleep(unsigned ms) { fake_now+=ms; }
int i2s_configure(const device *,int,const i2s_config *) { return 0; }
int i2s_trigger(const device *,int,int trigger) {
 if(trigger==I2S_TRIGGER_DROP) ++drops;
 if(trigger==I2S_TRIGGER_DRAIN) ++drains;
 return 0;
}
int i2s_write(const device *,void *data,std::size_t bytes) {
 CHECK(bytes==kBytes); ++writes; k_mem_slab_free(&audio_slab,data);
 if(after_write) after_write();
 return 0;
}
int main() {
 using pico::audio::Cue;
 pico::platform::Audio adapter;
 CHECK(adapter.init()); adapter.set_reference(true,69); adapter.play(Cue::ReferenceTone,25,69);
 SoundJob job{}; CHECK(k_msgq_get(&audio_queue,&job,K_NO_WAIT)==0);
 CHECK(job.note==69 && !cancelled(job));
 after_write=[&] { if(writes==3) adapter.set_reference(false,69); };
 play_cue(job);
 CHECK(writes==3 && drains==0 && drops==2 && audio_slab.used==0);
 CHECK(cancelled(job));
 adapter.set_reference(true,69); CHECK(cancelled(job)); // stale same-note job remains invalid
 adapter.play(Cue::ReferenceTone,25,69); CHECK(k_msgq_get(&audio_queue,&job,0)==0);
 CHECK(!cancelled(job));
 adapter.set_reference(true,40); CHECK(cancelled(job));
 adapter.play(Cue::ReferenceTone,25,40); CHECK(k_msgq_get(&audio_queue,&job,0)==0);
 CHECK(job.note==40); after_write={}; writes=drops=drains=0;
 play_cue(job); CHECK(writes>400 && drains==1 && audio_slab.used==0);
 adapter.set_muted(true); CHECK(cancelled(job));
 CHECK(k_msgq_num_used_get(&audio_queue)==0);
 if(failures) return EXIT_FAILURE;
 std::cout << "Reference audio worker model tests passed\n";
}
