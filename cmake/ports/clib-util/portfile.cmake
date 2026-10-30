# header-only library
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO powerof3/CLibUtil
    REF 993aa55e7d963ba844d53888302587ebabb69547
    SHA512 0419049fb92aee3e3ab0a592c410f11cd09db46814f35b822c12e80fb65076d606b3c39c48eaa663c3cac6817f3d947c06e0a7f0e6fc5b34406d60379c4e01bd
    HEAD_REF master
)

# Install codes
set(CLIBUTIL_SOURCE ${SOURCE_PATH}/include/ClibUtil)
file(INSTALL ${CLIBUTIL_SOURCE} DESTINATION ${CURRENT_PACKAGES_DIR}/include)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
