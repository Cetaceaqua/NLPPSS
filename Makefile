#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		:=	NLPPSS
BUILD		:=	build
SOURCES		:=	src src/ui src/ctr src/nlpp
INCLUDES	:=	inc inc/ui inc/ctr inc/nlpp
GRAPHICS	:=	
ROMFS		:=	romfs
GFXBUILD	:=	
ICON		:=	res/icon.png
APP_TITLE   :=	NLPP Save Studio
APP_AUTHOR  :=	Cetaceaqua
APP_DESCRIPTION :=	NEWラブプラス+ Save Studio

VERSION_MAJOR	:=	1
VERSION_MINOR	:=	1
VERSION_MICRO	:=	0
MAKEROM_VERARGS := -major $(VERSION_MAJOR) -minor $(VERSION_MINOR) -micro $(VERSION_MICRO)
APP_VERSION		:= v$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations -flto=auto \
			-fomit-frame-pointer -ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__ -DAPP_VERSION=\"$(APP_VERSION)\"

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -Wno-psabi -std=gnu++26

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lcitro2d -lcitro3d -lctru -lm

MO_FILES	:=  $(foreach lang,en_US zh_Hant,$(ROMFS)/locale/$(lang).mo)

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB) $(PORTLIBS)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(lastword $(subst /, ,$(CURDIR))))
#---------------------------------------------------------------------------------

export OUTPUT	:=	../$(TARGET)
export TOPDIR	:=	..

export VPATH	:=	$(foreach dir,$(SOURCES),../$(dir)) \
			$(foreach dir,$(GRAPHICS),../$(dir)) \
			$(foreach dir,$(DATA),../$(dir))

export DEPSDIR	:=	.

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export T3XFILES		:=	$(GFXFILES:.t3s=.t3x)

export ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export T3XHFILES		:=	$(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(if $(filter $(BUILD),$(GFXBUILD)),$(addsuffix .o,$(T3XFILES)))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I../$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I.

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := ../$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := ../icon.png
		endif
	endif
else
	export APP_ICON := ../$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(OUTPUT).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=../$(ROMFS)
endif

.PHONY: all clean pot mo

#---------------------------------------------------------------------------------
all: $(BUILD) mo $(ROMFS_T3XFILES) $(T3XHFILES)
	@[ -d $(BUILD) ] || mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f ../Makefile $(OUTPUT).3dsx

$(BUILD):
	@mkdir -p $@

ifeq ($(wildcard /mnt/d/Files/NLPP_CHS/3DS\ Toolkit/tools/makerom.exe),)
MAKEROM ?= makerom
else
MAKEROM ?= "/mnt/d/Files/NLPP_CHS/3DS Toolkit/tools/makerom.exe"
endif
STRIP   ?= $(DEVKITARM)/bin/arm-none-eabi-strip

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(GFXBUILD) $(ROMFS)/locale $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET)-strip.elf $(TARGET).cia $(TARGET).cci $(TARGET).3ds $(TARGET)_mode3.cia

$(TARGET)-strip.elf: $(TARGET).elf
	@$(STRIP) $(TARGET).elf -o $(TARGET)-strip.elf

$(TARGET).smdh: $(ICON)
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" "$<" "$@"

cia: $(TARGET)-strip.elf $(TARGET).smdh res/$(TARGET).bnr
	@$(MAKEROM) -f cia -o $(TARGET).cia -elf $(TARGET)-strip.elf -rsf res/$(TARGET).rsf -exefslogo -target t -icon $(TARGET).smdh -banner res/$(TARGET).bnr -DAPP_SYSTEM_MODE="64MB" $(MAKEROM_VERARGS)
	@echo Built $(TARGET).cia

cci: $(TARGET)-strip.elf $(TARGET).smdh res/$(TARGET).bnr
	@$(MAKEROM) -f cci -o $(TARGET).cci -elf $(TARGET)-strip.elf -rsf res/$(TARGET).rsf -exefslogo -target t -icon $(TARGET).smdh -banner res/$(TARGET).bnr -DAPP_SYSTEM_MODE="64MB" $(MAKEROM_VERARGS)
	@cp -f $(TARGET).cci $(TARGET).3ds
	@if [ -d "/mnt/d/Files/Azahar" ]; then cp -f $(TARGET).cci "/mnt/d/Files/Azahar/$(TARGET).cci"; echo "Copied to /mnt/d/Files/Azahar/$(TARGET).cci"; fi
	@echo Built $(TARGET).cci and $(TARGET).3ds

all_formats: all cia cci

send:
	@3dslink $(TARGET).3dsx

$(MO_FILES): $(ROMFS)/locale/%.mo: po/%.po
	@echo "Building $@"
	@[ -d $(dir $@) ] || mkdir -p $(dir $@)
	python3 msgfmt.py -o $@ $<

po/%.po: $(notdir $(TARGET)).pot
	@echo "Building $@"
	@[ -d $(dir $@) ] || mkdir -p $(dir $@)
	@if [ -f $@ ]; \
	then \
		msgmerge --backup=off -qU $@ $< && touch $@; \
	else \
		msginit -i $< --no-translator -l $*.UTF-8 -o $@; \
	fi

pot: $(CFILES) $(CPPFILES)
	@echo "Building $(TARGET).pot"
	xgettext -o $(TARGET).pot $^ inc/ui.h -kgetText -kgetTxt -c \
		--from-code="UTF-8" \
		--copyright-holder="R-YaTian" \
		--package-name="$(APP_TITLE)" \
		--package-version="$(APP_VERSION)" \
		--msgid-bugs-address="gmtianya@gmail.com"

mo: $(MO_FILES)

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	$(GRAPHICS)/%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@mkdir -p $(BUILD) $(GFXBUILD)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(BUILD)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.t3x
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	@$(bin2o)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $*.shbin)
	$(eval DEPSFILE := $(DEPSDIR)/$*.shbin.d)
	echo "$(CURBIN).o: $< $1" > $(DEPSFILE)
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $*.shbin.o
endef

%.shbin.o %_shbin.h : %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o %_shbin.h : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o %_shbin.h : %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)$(file)))

#---------------------------------------------------------------------------------
%.t3x	%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $*.h -d $*.d -o $*.t3x

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
