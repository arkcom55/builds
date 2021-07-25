include $(TOPDIR)/rules.mk

# Name, version and release number
# The name and version of your package are used to define the variable to point to the build directory of your package: $(PKG_BUILD_DIR)
PKG_NAME:=mlvpn
PKG_VERSION:=1.1
PKG_RELEASE:=237

# Source settings (i.e. where to find the source codes)
# This is a custom variable, used below
SOURCE_DIR:=/home/ned/nednet2b/mlvpn

include $(INCLUDE_DIR)/package.mk

# Package definition; instructs on how and where our package will appear in the overall configuration menu ('make menuconfig')
define Package/mlvpn
  SECTION:=nednet
  CATEGORY:=NedNet
  TITLE:=MLVPN!
  DEPENDS:=+libev 
endef
#DEPENDS:=+libev +libsodium +libpcap

# Package description; a more verbose description on what our package does
define Package/mlvpn/description
  MLVPN application.
endef

# Package preparation instructions; create the build directory and copy the source code. 
# The last command is necessary to ensure our preparation instructions remain compatible with the patching system.
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp $(SOURCE_DIR)/* $(PKG_BUILD_DIR)
	$(Build/Patch)
endef

# Package build instructions; invoke the target-specific compiler to first compile the source file, and then to link the file into the final executable
#define Build/Compile
#	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/nednet2bcore.o -c $(PKG_BUILD_DIR)/nednet2bcore.c
#	$(TARGET_CC) $(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/$1 $(PKG_BUILD_DIR)/nednet2bcore.o $(PKG_BUILD_DIR)/cJSON.o $(PKG_BUILD_DIR)/nednet2bcommon.o -lcurl
#endef
define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/mlvpn.o -c $(PKG_BUILD_DIR)/mlvpn.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/rtun_send.o -c $(PKG_BUILD_DIR)/rtun_send.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/rtun_receive.o -c $(PKG_BUILD_DIR)/rtun_receive.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/buffer.o -c $(PKG_BUILD_DIR)/buffer.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/configlib.o -c $(PKG_BUILD_DIR)/configlib.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/config.o -c $(PKG_BUILD_DIR)/config.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/control.o -c $(PKG_BUILD_DIR)/control.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/tool.o -c $(PKG_BUILD_DIR)/tool.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/privsep.o -c $(PKG_BUILD_DIR)/privsep.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/privsep_fdpass.o -c $(PKG_BUILD_DIR)/privsep_fdpass.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/wrr.o -c $(PKG_BUILD_DIR)/wrr.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/log.o -c $(PKG_BUILD_DIR)/log.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/reorder.o -c $(PKG_BUILD_DIR)/reorder.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/timestamp.o -c $(PKG_BUILD_DIR)/timestamp.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/tuntap_generic.o -c $(PKG_BUILD_DIR)/tuntap_generic.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/tuntap_linux.o -c $(PKG_BUILD_DIR)/tuntap_linux.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/systemd.o -c $(PKG_BUILD_DIR)/systemd.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/setproctitle.o -c $(PKG_BUILD_DIR)/setproctitle.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/filters.o -c $(PKG_BUILD_DIR)/filters.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/closefrom.o -c $(PKG_BUILD_DIR)/closefrom.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/vis.o -c $(PKG_BUILD_DIR)/vis.c
	$(TARGET_CC) $(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/$1 $(PKG_BUILD_DIR)/mlvpn.o \
		$(PKG_BUILD_DIR)/rtun_send.o $(PKG_BUILD_DIR)/rtun_receive.o \
		$(PKG_BUILD_DIR)/buffer.o $(PKG_BUILD_DIR)/configlib.o $(PKG_BUILD_DIR)/config.o \
		$(PKG_BUILD_DIR)/tool.o $(PKG_BUILD_DIR)/privsep.o $(PKG_BUILD_DIR)/privsep_fdpass.o \
		$(PKG_BUILD_DIR)/wrr.o $(PKG_BUILD_DIR)/log.o \
		$(PKG_BUILD_DIR)/reorder.o $(PKG_BUILD_DIR)/timestamp.o $(PKG_BUILD_DIR)/tuntap_generic.o \
		$(PKG_BUILD_DIR)/tuntap_linux.o $(PKG_BUILD_DIR)/systemd.o $(PKG_BUILD_DIR)/setproctitle.o \
		$(PKG_BUILD_DIR)/control.o $(PKG_BUILD_DIR)/filters.o $(PKG_BUILD_DIR)/closefrom.o  \
		$(PKG_BUILD_DIR)/vis.o -lev 
endef
# 		$(PKG_BUILD_DIR)/vis.o -lev -lsodium -lpcap
# 		$(PKG_BUILD_DIR)/wrr.o $(PKG_BUILD_DIR)/crypto.o $(PKG_BUILD_DIR)/log.o \
#	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/crypto.o -c $(PKG_BUILD_DIR)/crypto.c

# Package install instructions; create a directory inside the package to hold our executable, and then copy the executable we built previously into the folder
define Package/mlvpn/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/mlvpn $(1)/usr/bin
endef

# This command is always the last, it uses the definitions and variables we give above in order to get the job done
$(eval $(call BuildPackage,mlvpn))
