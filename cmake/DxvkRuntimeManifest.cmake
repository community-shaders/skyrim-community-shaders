# source category | installed name | component | required
#
# This is the authoritative inventory for the Vulkan/DXVK runtime directory.
# Package sources are already copied by the base package install; generated
# sources are staged explicitly below. Keeping their destination names here
# lets install, AIO preparation, cleanup, and CI validate the same contract.
set(CS_DXVK_RUNTIME_MANIFEST
    "package|amd_fidelityfx_vk.dll|Streamline|required"
    "package|libxess.dll|Streamline|required"
    "package|license.txt|Streamline|required"
    "package|NvLowLatencyVk.dll|Streamline|required"
    "package|nvngx_dlss.dll|Streamline|required"
    "package|nvngx_dlss.license.txt|Streamline|required"
    "package|nvngx_dlssg.dll|Streamline|required"
    "package|reflex.license.txt|Streamline|required"
    "package|renderdoc.dll|RenderDoc|optional"
    "package|sl.common.dll|Streamline|required"
    "package|sl.dlss.dll|Streamline|required"
    "package|sl.dlss_g.dll|Streamline|required"
    "package|sl.pcl.dll|Streamline|required"
    "package|sl.reflex.dll|Streamline|required"
    "generated|sl.fsr.dll|Streamline|required"
    "generated|sl.fsr_g.dll|Streamline|required"
    "generated|sl.interposer.dll|Streamline|required"
    "generated|sl.xess.dll|Streamline|required"
    "generated|dxvk_d3d11.dll|DXVK|required"
    "generated|dxvk_dxgi.dll|DXVK|required"
    "license|dxvk.license.txt|DXVK|required"
    "license|fidelityfx.license.txt|Streamline|required"
    "license|xess.license.txt|Streamline|required"
    "license|xess.third-party-programs.txt|Streamline|required"
)

function(cs_write_dxvk_runtime_manifest output_file)
    set(_contents "")
    foreach(_entry IN LISTS CS_DXVK_RUNTIME_MANIFEST)
        string(REPLACE "|" ";" _fields "${_entry}")
        list(GET _fields 1 _name)
        list(GET _fields 3 _requirement)
        if(_requirement STREQUAL "required")
            string(APPEND _contents "${_name}\n")
        endif()
    endforeach()
    file(WRITE "${output_file}" "${_contents}")
endfunction()
