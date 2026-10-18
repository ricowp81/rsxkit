# SPDX-License-Identifier: MIT
#
# rsxkit demo — builds with PSL1GHT (PSL1GHT and PS3DEV must be set).
#
#   make        -> build/rsxkit-demo.elf, .self, .fake.self
#   make pkg    -> build/rsxkit-demo.pkg (retail-style package, installable)
#   make clean

ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT is not set)
endif

PROGRAM := rsxkit-demo
TITLE   := rsxkit demo
APPID   := RSXK00001
CONTENTID ?= UP0001-$(APPID)_00-0000000000000000

HERE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
LIB  := $(abspath $(HERE)/../..)
OUT  := $(HERE)/build
OBJ  := $(OUT)/obj

ifneq ($(V),1)
  VERB := @
endif

MACHDEP := -mhard-float -fmodulo-sched -ffunction-sections -fdata-sections
CFLAGS  := -std=gnu11 -O2 -Wall -Wextra -mcpu=cell $(MACHDEP) \
           -I$(PSL1GHT)/ppu/include -I$(LIB)/include

.PHONY: all pkg clean lib
.SECONDARY:
all: $(OUT)/$(PROGRAM).elf $(OUT)/$(PROGRAM).self $(OUT)/$(PROGRAM).fake.self
pkg: $(OUT)/$(PROGRAM).pkg

lib:
	$(VERB)$(MAKE) --no-print-directory -C $(LIB) OUT=$(OUT)/rsxkit
$(OUT)/rsxkit/librsxkit.a: lib ;

$(OBJ)/main.o: $(HERE)/main.c
	@echo "  PPU CC  main.c"
	@mkdir -p $(@D)
	$(VERB)ppu-gcc $(CFLAGS) -c $< -o $@

$(OBJ)/$(PROGRAM).raw.elf: $(OBJ)/main.o $(OUT)/rsxkit/librsxkit.a
	@echo "  PPU LD  $(notdir $@)"
	$(VERB)ppu-gcc $(MACHDEP) -L$(PSL1GHT)/ppu/lib $^ -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm -o $@

$(OUT)/$(PROGRAM).elf: $(OBJ)/$(PROGRAM).raw.elf
	@echo "  ELF     $(notdir $@)"
	$(VERB)ppu-strip $< -o $@
	$(VERB)sprxlinker $@

$(OUT)/$(PROGRAM).self: $(OUT)/$(PROGRAM).elf
	@echo "  SELF    $(notdir $@)"
	$(VERB)make_self $< $@ >/dev/null

$(OUT)/$(PROGRAM).fake.self: $(OUT)/$(PROGRAM).elf
	@echo "  FSELF   $(notdir $@)"
	$(VERB)fself $< $@

ICON0 ?= $(PS3DEV)/bin/ICON0.PNG

$(OUT)/$(PROGRAM).pkg: $(OUT)/$(PROGRAM).elf $(ICON0)
	@echo "  PKG     $(notdir $@)"
	$(VERB)rm -rf $(OBJ)/pkg && mkdir -p $(OBJ)/pkg/USRDIR
	$(VERB)cp $(ICON0) $(OBJ)/pkg/ICON0.PNG
	$(VERB)make_self_npdrm $< $(OBJ)/pkg/USRDIR/EBOOT.BIN $(CONTENTID) >/dev/null
	$(VERB)sfo --title "$(TITLE)" --appid "$(APPID)" -f $(PS3DEV)/bin/sfo.xml $(OBJ)/pkg/PARAM.SFO
	$(VERB)pkg --contentid $(CONTENTID) $(OBJ)/pkg/ $@ >/dev/null
	$(VERB)cp $@ $(basename $@).gnpdrm.pkg
	$(VERB)package_finalize $(basename $@).gnpdrm.pkg

clean:
	rm -rf $(OUT)
