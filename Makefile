ROOTDIR=$(shell pwd)
WORKDIR=$(ROOTDIR)/build


ARCH								:= MT7620

ifeq ($(ARCH),MT7620)
CROSSTOOLDIR 				:= /home/Software/OpenWrt-SDK
CROSS   						:= mipsel-openwrt-linux-
export  STAGING_DIR	:= $(CROSSTOOLDIR)/staging_dir
export  PATH				:= $(PATH):$(STAGING_DIR)/toolchain-mipsel_24kec+dsp_gcc-4.8-linaro_uClibc-0.9.33.2/bin
CROSS_CFLAGS				:= -I$(CROSSTOOLDIR)/staging_dir/toolchain-mipsel_24kec+dsp_gcc-4.8-linaro_uClibc-0.9.33.2/usr/include
CROSS_CFLAGS				+= -I$(CROSSTOOLDIR)/staging_dir/target-mipsel_24kec+dsp_uClibc-0.9.33.2/usr/include
CROSS_CFLAGS				+= -I$(CROSSTOOLDIR)/staging_dir/target-mipsel_24kec+dsp_uClibc-0.9.33.2/usr/include/glib-2.0
CROSS_CFLAGS				+= -I$(CROSSTOOLDIR)/staging_dir/target-mipsel_24kec+dsp_uClibc-0.9.33.2/usr/include/dbus-1.0
CROSS_CFLAGS				+= -I$(CROSSTOOLDIR)/staging_dir/target-mipsel_24kec+dsp_uClibc-0.9.33.2/usr/lib/dbus-1.0/include/

CROSS_LDFLAGS				:= -L$(CROSSTOOLDIR)/staging_dir/toolchain-mipsel_24kec+dsp_gcc-4.8-linaro_uClibc-0.9.33.2/usr/lib
CROSS_LDFLAGS				+= -L$(CROSSTOOLDIR)/staging_dir/target-mipsel_24kec+dsp_uClibc-0.9.33.2/usr/lib/ 
endif


all : test

srcs	:= $(ROOTDIR)/src/main.c
srcs	+= $(ROOTDIR)/src/bp.c
srcs	+= $(ROOTDIR)/src/curl.c
srcs	+= $(ROOTDIR)/src/dev.c
srcs	+= $(ROOTDIR)/src/gm.c
srcs	+= $(ROOTDIR)/src/lock.c
srcs	+= $(ROOTDIR)/src/main.c
srcs	+= $(ROOTDIR)/src/Oximeter.c 
srcs	+= $(ROOTDIR)/src/util.c  
srcs	+= $(ROOTDIR)/src/wt.c

srcs	+= $(ROOTDIR)/src/gdbus/client.c
srcs	+= $(ROOTDIR)/src/gdbus/mainloop.c
srcs	+= $(ROOTDIR)/src/gdbus/object.c
srcs	+= $(ROOTDIR)/src/gdbus/polkit.c
srcs	+= $(ROOTDIR)/src/gdbus/watch.c


objs	:= $(subst $(ROOTDIR),$(WORKDIR), $(subst .c,.o,$(srcs)))

LDFLAGS	:= -ldbus-1 -ljson-c -lcurl -lssl -lcrypto -lglib-2.0 -lmbedtls

CFLAGS	:= -I$(ROOTDIR)/src/gdbus -I$(ROOTDIR)/src/monitor

TARGET_CFLAGS 	:= $(CROSS_CFLAGS)
TARGET_CXXFLAGS	:= $(TARGET_CFLAGS)

TARGET_LDFLAGS	:= $(CROSS_LDFLAGS) 

GCC		:= $(CROSS)gcc
CXX		:= $(CROSS)g++
MKDIR	:= mkdir -p

test : $(objs)
	$(GCC) $^ $(LDFLAGS) $(TARGET_LDFLAGS) -o $(ROOTDIR)/build/$@

$(ROOTDIR)/build/%.o : $(ROOTDIR)/%.c
	@$(MKDIR) $(dir $@)
	@echo $(GCC) -c $< $(CFLAGS) $(TARGET_CFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -o $@
	$(GCC) -c $< $(CFLAGS) $(TARGET_CFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -o $@

$(ROOTDIR)/build/%.o : $(ROOTDIR)/%.cpp
	@$(MKDIR) $(dir $@)
	@echo $(CXX) -c $< $(CXXFLAGS) $(TARGET_CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -o $@
	$(CXX) -c $< $(CXXFLAGS) $(TARGET_CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -o $@


clean: 
	rm -rf ./build


scp :
	scp -P2200 ./build/test root@192.168.0.230:/root
