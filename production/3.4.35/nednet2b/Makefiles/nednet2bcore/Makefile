include $(TOPDIR)/rules.mk

# Name, version and release number
# The name and version of your package are used to define the variable to point to the build directory of your package: $(PKG_BUILD_DIR)
PKG_NAME:=nednet2bcore
PKG_VERSION:=3.5
PKG_RELEASE:=9

# Source settings (i.e. where to find the source codes)
# This is a custom variable, used below
SOURCE_DIR:=/home/ned/nednet2b/nednet2bcore

include $(INCLUDE_DIR)/package.mk

# Package definition; instructs on how and where our package will appear in the overall configuration menu ('make menuconfig')
define Package/nednet2bcore
  SECTION:=nednet
  CATEGORY:=NedNet
  TITLE:=Ned Net 2b!
  DEPENDS:=+libcurl
endef

# Package description; a more verbose description on what our package does
define Package/nednet2bcore/description
  Ned Net 2b core application.
endef

# Package preparation instructions; create the build directory and copy the source code. 
# The last command is necessary to ensure our preparation instructions remain compatible with the patching system.
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp $(SOURCE_DIR)/* $(PKG_BUILD_DIR)
	$(Build/Patch)
endef

# Package build instructions; invoke the target-specific compiler to first compile the source file, and then to link the file into the final executable
define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/nednet2bcore.o -c $(PKG_BUILD_DIR)/nednet2bcore.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/cJSON.o -c $(PKG_BUILD_DIR)/cJSON.c
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/nednet2bcommon.o -c $(PKG_BUILD_DIR)/nednet2bcommon.c
	$(TARGET_CC) $(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/$1 $(PKG_BUILD_DIR)/nednet2bcore.o $(PKG_BUILD_DIR)/cJSON.o $(PKG_BUILD_DIR)/nednet2bcommon.o -lcurl
endef

# Package install instructions; create a directory inside the package to hold our executable, and then copy the executable we built previously into the folder
define Package/nednet2bcore/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/nednet2bcore $(1)/usr/bin
endef

# This command is always the last, it uses the definitions and variables we give above in order to get the job done
$(eval $(call BuildPackage,nednet2bcore))
