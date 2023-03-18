#include "remote_receiver_nf.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#define INC_BUFFER_PTR(var, limit) ((var) >= limit ? 0 : ((var) + 1))
#define DEC_BUFFER_PTR(var, limit) ((var) == 0 ? limit : ((var) - 1))
static const uint8_t MIN_NONIDLE_DIST = 6;
;

namespace esphome {
namespace remote_receiver_nf {

static const char *const TAG = "remote_receiver_nf";

void IRAM_ATTR HOT RemoteReceiverNFComponentStore::gpio_intr(RemoteReceiverNFComponentStore *arg)
{
  const bool level = arg->pin.digital_read();
  const uint32_t now = micros();

  uint32_t next = arg->buffer_write_at;
  next = INC_BUFFER_PTR(next, arg->buffer_size_limit);
  if (next == 0 ) arg->loop++;
  if ( level != (next & 1) ) return;
  arg->overflow |= (next == arg->buffer_read_at);
  arg->buffer[arg->buffer_write_at = next] = now;
}


void RemoteReceiverNFComponent::preprocessing()
{
	static uint32_t loop_cnt = 0;
	static uint32_t overflow_cnt = 0;
	auto &s = this->store_;
	const uint32_t write_at = s.buffer_write_at;
	if (s.overflow) {
		// in case of overflow, discard everything
		s.buffer_read_at = write_at;
		s.overflow = false;
		overflow_cnt ++;
		return;
	}
	if ( s.buffer_read_at == write_at ) return;
	bool idle = (micros() - s.buffer[write_at]) >= this->idle_us_;

	loop_cnt++;
//	ESP_LOGD(TAG, "read ptr = %d, write_ptr = %d, loop counter =%d, overflow_cnt=%d, interrupt loop=%d", s.buffer_read_at, write_at,loop_cnt, overflow_cnt, s.loop);
	uint32_t prev_data = s.buffer[s.buffer_read_at];
	for (uint32_t i= s.buffer_read_at; i != write_at; ) {
		i = INC_BUFFER_PTR(i, this->buffer_size_limit_ );
		uint32_t	cur_data = s.buffer[i];
		uint32_t	delta = cur_data - prev_data;
		prev_data = cur_data;
		if ( delta < filter_us_ ) {
			if ( pulse_process_at_ != pulse_write_at_ ) {
				pulse_buffer_[pulse_process_at_] += (pulse_buffer_[pulse_process_at_] > 0 ) ? delta : -delta;
			}else{
				continue;
			}
		}else {
			uint32_t	next = INC_BUFFER_PTR( pulse_process_at_, buffer_size_limit_);
			int32_t		prev_data = pulse_buffer_[pulse_process_at_];
			pulse_buffer_[next] = (i & 1) ? -delta : delta;
			if ( (next - pulse_write_at_) > 2 ) {
				if ( space_lvl_high_ ? ( (prev_data < sync_space_max_us_) && (prev_data > sync_space_min_us_) ) :
						( ( (-prev_data) < sync_space_max_us_) && ( (-prev_data) > sync_space_min_us_)) ){
					// found sync space
					if ( recv_data_ ) {
						// put idle in place
						uint32_t idle_loc = ( pulse_process_at_ + this->buffer_size_limit_ -1 ) % ( this->buffer_size_limit_ +1 );
						pulse_buffer_[idle_loc] = pulse_buffer_[idle_loc] > 0 ? idle_us_ : -idle_us_;
						pulse_write_at_ = idle_loc;
					}else {
						uint32_t first_loc = DEC_BUFFER_PTR( pulse_process_at_, this->buffer_size_limit_ );
						// fill noise with zeros
						for ( uint32_t k = INC_BUFFER_PTR(pulse_write_at_, this->buffer_size_limit_); k!= first_loc; k = INC_BUFFER_PTR(k, this->buffer_size_limit_)){
							pulse_buffer_[k] = 0;
						}
						pulse_write_at_ = DEC_BUFFER_PTR(first_loc, this->buffer_size_limit_);
						recv_data_ = true;
					}
				}else {
					// check if it is idle
					if ( recv_data_ ) {
						// previous data is idle
						if ( prev_data > 0 ? (prev_data >= idle_us_ ) : (prev_data < -idle_us_ ) ){
							recv_data_ = false;
							pulse_write_at_ = pulse_process_at_;
						}else if (delta >=idle_us_ ) {
							recv_data_ = false;
							pulse_write_at_ = next;
						}else if ( (next - pulse_write_at_) > num_edge_max_) {
							// sometime went wrong, message is too long, discard it.
							// the data would be cleared by the end of loop or next sync pattern detected.
							recv_data_ = false;
							ESP_LOGW(TAG,"too noisy, drop the message");
						}
					}
				}
				
			}
			pulse_process_at_ = next;
		}
	}
	s.buffer_read_at = write_at;
	if ( recv_data_ ) {
		if ( idle ) {
			if ( space_lvl_high_ ? ( pulse_buffer_[pulse_process_at_] < 0 ) : (pulse_buffer_[pulse_process_at_] > 0 ) ) {
				pulse_process_at_ = INC_BUFFER_PTR(pulse_process_at_, this->buffer_size_limit_);
			}
			pulse_buffer_[pulse_process_at_] = space_lvl_high_ ? idle_us_ : - idle_us_;
			pulse_write_at_ = pulse_process_at_;
			recv_data_ = false;
		}
	}else {
		if ( idle ) {
			// drop all pending noise
			pulse_process_at_ = pulse_write_at_;
		}else {
			if ( (pulse_process_at_ - pulse_write_at_) > 2 ) {
				// currently pulse_process_at_ maybe a sync space 
				// clear noise
				uint32_t pending_loc = DEC_BUFFER_PTR(pulse_process_at_, this->buffer_size_limit_ ); 

				for ( uint32_t i = INC_BUFFER_PTR(pulse_write_at_, this->buffer_size_limit_); i!= pending_loc; i = INC_BUFFER_PTR(i, this->buffer_size_limit_)){
					pulse_buffer_[i] = 0;
				}
				pulse_write_at_ = DEC_BUFFER_PTR(pending_loc, this->buffer_size_limit_);
			}
		}
	}
}

void RemoteReceiverNFComponent::setup()
{
  ESP_LOGCONFIG(TAG, "Setting up Remote Receiver With Noise Filtering...");
  this->pin_->setup();
  auto &s = this->store_;
  s.pin = this->pin_->to_isr();
  s.buffer_size_limit = this->buffer_size_limit_ ;
  uint32_t buffer_size = this->buffer_size_limit_+1;

  this->high_freq_.start();
  if (!(buffer_size & 1)) {
    // Make sure divisible by two. This way, we know that every 0bxxx0 index is a space and every 0bxxx1 index is a mark
    s.buffer_size_limit++;
    buffer_size++;
  }

  s.buffer = new uint32_t[buffer_size];
  void *buf = (void *) s.buffer;
  memset(buf, 0, buffer_size * sizeof(uint32_t));

  this->pulse_buffer_ = new int32_t[buffer_size];
  buf = (void *) this->pulse_buffer_;
  memset(buf, 0, buffer_size * sizeof(int32_t));

  // First index is a space.
  if (this->space_lvl_high_) {
    s.buffer_write_at = s.buffer_read_at = 1;
  } else {
    s.buffer_write_at = s.buffer_read_at = 0;
  }
  this->pin_->attach_interrupt(RemoteReceiverNFComponentStore::gpio_intr, &this->store_, gpio::INTERRUPT_ANY_EDGE);
  s.loop = 0;
}

void RemoteReceiverNFComponent::dump_config()
{
  ESP_LOGCONFIG(TAG, "Remote Receiver NF:");
  LOG_PIN("  Pin: ", this->pin_);
/*
  if (this->pin_->digital_read()) {
    ESP_LOGW(TAG, "Remote Receiver Signal starts with a HIGH value. Usually this means you have to "
                  "invert the signal using 'inverted: True' in the pin schema!");
  }
*/
  ESP_LOGCONFIG(TAG, "  Buffer Size: %u", this->buffer_size_limit_+1);
  ESP_LOGCONFIG(TAG, "  Tolerance: %u%%", this->tolerance_);
  ESP_LOGCONFIG(TAG, "  Filter out pulses shorter than: %u us", this->filter_us_);
  ESP_LOGCONFIG(TAG, "  Signal is done after %u us of no changes", this->idle_us_);
  ESP_LOGCONFIG(TAG, "  Level of signal space is %s", this->space_lvl_high_ ? "high" : "low");
  ESP_LOGCONFIG(TAG, "  Sync space is between %u us and %u us", this->sync_space_min_us_, this->sync_space_max_us_);
  ESP_LOGCONFIG(TAG, "  Number of edges for the protocol: min=%d, max=%d ", this->num_edge_min_, this->num_edge_max_);
}

void RemoteReceiverNFComponent::loop()
{
	uint32_t	tmp_ptr;

	uint32_t	len = 0;
	preprocessing();

	if ( pulse_read_at_ == pulse_write_at_ ) return;
	// remove leading zeros
	do {
		tmp_ptr = INC_BUFFER_PTR(pulse_read_at_, this->buffer_size_limit_);
		if ( pulse_buffer_[tmp_ptr] !=0 ) break;
		pulse_read_at_ = tmp_ptr;
	}while ( pulse_read_at_ != pulse_write_at_);
	if ( pulse_read_at_ == pulse_write_at_ ) return;
	tmp_ptr = pulse_read_at_;
	do {
		len ++;
		this->pulse_read_at_ = INC_BUFFER_PTR(this->pulse_read_at_, this->buffer_size_limit_);
		if (pulse_buffer_[this->pulse_read_at_] ==0 ) {
			break;
		}
		if ( space_lvl_high_ ? pulse_buffer_[this->pulse_read_at_] > idle_us_ : pulse_buffer_[this->pulse_read_at_] < -idle_us_ ) {
			break;
		}
	}while(this->pulse_read_at_!= pulse_write_at_);

	if ( len < num_edge_min_ ) {
		// to short skip the capture
		return;
	}
	
	this->temp_.clear();
	this->temp_.reserve(len);
	for ( uint32_t i = 0; i < len; i++) {
		tmp_ptr = INC_BUFFER_PTR(tmp_ptr, this->buffer_size_limit_);
		uint32_t val = pulse_buffer_[tmp_ptr];
		if ( val ) temp_.push_back(val);
	}
		
	ESP_LOGD(TAG, "	write_at=%d, buffer len = %d  buffer[0]=%d, buffer[1]=%d, buffer[last]=%d", pulse_write_at_,len, temp_[0], temp_[1], temp_[len-1]);


	this->call_listeners_dumpers_();
}

}  // namespace remote_receiver_nf
}  // namespace esphome
