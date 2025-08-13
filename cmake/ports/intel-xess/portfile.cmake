# Intel XeSS SDK - headers only
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO intel/xess
    REF v2.1.0
    SHA512 6129abf9a271c366e8d04f2676ec8f39858cd8e1530b0178911a0c5e1c616db56bc6c577aa3cec2d63f23310cedb658f5e7b463469bb467482bb40af59ed155a
    HEAD_REF main
)

# Install only the necessary header files (not the entire repo)
set(XESS_HEADERS_SOURCE ${SOURCE_PATH}/inc/xess)
file(INSTALL ${XESS_HEADERS_SOURCE} DESTINATION ${CURRENT_PACKAGES_DIR}/include)

set(XESS_FG_HEADERS_SOURCE ${SOURCE_PATH}/inc/xess_fg)
file(INSTALL ${XESS_FG_HEADERS_SOURCE} DESTINATION ${CURRENT_PACKAGES_DIR}/include)

# Install copyright
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")