###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
	$(error please specify PLATFORM, eg. PLATFORM=trimui make)
endif

ifeq (,$(CROSS_COMPILE))
	$(error missing CROSS_COMPILE for this toolchain)
endif

###########################################################

include ../../$(PLATFORM)/platform/makefile.env
SDL?=SDL

###########################################################

TARGET = minarch
INCDIR = -I. -I./libretro-common/include/ -I../common/ -I../../$(PLATFORM)/platform/
SOURCE = $(TARGET).c ../common/scaler.c ../common/utils.c ../common/api.c ../../$(PLATFORM)/platform/platform.c

# RetroAchievements sin conexión (logros.c) con rcheevos y libchdr compilados dentro
RCHEEVOS_VERSION = 40d916de00fe757bab40fb4db41a7912193a48e3
LIBCHDR_VERSION = 47d7941
RC = rcheevos/src/src
SOURCE += logros.c chd_reader.c \
	$(RC)/rc_client.c $(RC)/rc_compat.c $(RC)/rc_util.c $(RC)/rc_version.c $(RC)/rc_libretro.c \
	$(RC)/rcheevos/alloc.c $(RC)/rcheevos/condition.c $(RC)/rcheevos/condset.c $(RC)/rcheevos/consoleinfo.c \
	$(RC)/rcheevos/format.c $(RC)/rcheevos/lboard.c $(RC)/rcheevos/memref.c $(RC)/rcheevos/operand.c \
	$(RC)/rcheevos/richpresence.c $(RC)/rcheevos/runtime.c $(RC)/rcheevos/runtime_progress.c \
	$(RC)/rcheevos/trigger.c $(RC)/rcheevos/value.c $(RC)/rcheevos/rc_validate.c \
	$(RC)/rapi/rc_api_common.c $(RC)/rapi/rc_api_editor.c $(RC)/rapi/rc_api_info.c \
	$(RC)/rapi/rc_api_runtime.c $(RC)/rapi/rc_api_user.c \
	$(RC)/rhash/cdreader.c $(RC)/rhash/hash.c $(RC)/rhash/hash_disc.c $(RC)/rhash/hash_rom.c \
	$(RC)/rhash/hash_zip.c $(RC)/rhash/md5.c \
	$(addprefix libchdr/src/libchdr_,bitstream.c cdrom.c chd.c codec_avhuff.c codec_cdfl.c codec_cdlz.c \
		codec_cdzl.c codec_cdzs.c codec_flac.c codec_huff.c codec_lzma.c codec_zlib.c codec_zstd.c flac.c huffman.c) \
	libchdr/deps/lzma-26.02/src/LzmaDec.c libchdr/deps/zstd-1.5.7/zstddeclib.c
INCDIR += -Ircheevos/src/include -I$(RC) -Ilibchdr/include -Ilibchdr/deps/zstd-1.5.7

CC = $(CROSS_COMPILE)gcc
CFLAGS   = $(ARCH) -fomit-frame-pointer
CFLAGS  += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -DUSE_$(SDL) -Ofast -std=gnu99
CFLAGS  += -DRC_DISABLE_LUA -DRC_CLIENT_SUPPORTS_HASH -DRC_HASH_NO_ENCRYPTED
CFLAGS  += -DCHDR_SYSTEM_ZLIB -DWANT_RAW_DATA_SECTOR=1 -DWANT_SUBCODE=1 -DVERIFY_BLOCK_CRC=0 -DLOWRAM_TARGET=1
CFLAGS	+= -Os -flto
LDFLAGS	 = -ldl $(LIBS) -lmsettings -l$(SDL) -l$(SDL)_image -l$(SDL)_ttf -lpthread -lm -lz
# CFLAGS  += -Wall -Wno-unused-variable -Wno-unused-function -Wno-format-overflow
# CFLAGS  += -fsanitize=address -fno-common
# LDFLAGS += -lasan

BUILD_DATE!=date +%Y.%m.%d
BUILD_HASH!=cat ../../hash.txt
CFLAGS += -DBUILD_DATE=\"${BUILD_DATE}\" -DBUILD_HASH=\"${BUILD_HASH}\"

# CFLAGS += -DBUILD_DATE=\"20230326\" -DBUILD_HASH=\"d34db33f\"

PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all: libretro-common rcheevos/src libchdr $(PREFIX)/include/msettings.h
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -f $(PRODUCT)

libretro-common:
	git clone https://github.com/libretro/libretro-common

rcheevos/src:
	git clone https://github.com/RetroAchievements/rcheevos.git rcheevos/src
	cd rcheevos/src && git checkout $(RCHEEVOS_VERSION)

libchdr:
	git clone https://github.com/rtissera/libchdr
	cd libchdr && git checkout $(LIBCHDR_VERSION)
	
$(PREFIX)/include/msettings.h:
	cd /root/workspace/$(PLATFORM)/libmsettings && make