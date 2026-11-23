#pragma once
#include <zephyr/device.h>
#include <zephyr/kernel.h>
struct i2s_config { unsigned word_size,channels,format,options; std::uint32_t frame_clk_freq; k_mem_slab *mem_slab; std::size_t block_size; int timeout; };
#define I2S_DIR_TX 1
enum i2s_trigger_cmd { I2S_TRIGGER_DROP, I2S_TRIGGER_START, I2S_TRIGGER_DRAIN };
#define I2S_FMT_DATA_FORMAT_I2S 1
#define I2S_OPT_FRAME_CLK_CONTROLLER 1
#define I2S_OPT_BIT_CLK_CONTROLLER 2
int i2s_configure(const device *,int,const i2s_config *);
int i2s_trigger(const device *,int,i2s_trigger_cmd);
int i2s_write(const device *,void *,std::size_t);
