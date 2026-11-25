# SPDX-License-Identifier: MIT
# Keep the pinned checkout immutable. Replace only the ROM service TU in its
# normal target so its compile definitions/member-pointer ABI stay intact.
function(gcnllr_adapt_dsp_rom target)
  get_target_property(_done "${target}" GCNLLR_DSP_ROM_CONTRACT)
  if(_done)
    return()
  endif()
  get_target_property(_sources "${target}" SOURCES)
  set(_count 0)
  set(_adapted_sources)
  foreach(_source IN LISTS _sources)
    if(_source MATCHES "/Core/HW/DSPHLE/UCodes/ROM\\.cpp$")
      math(EXPR _count "${_count}+1")
      file(READ "${_source}" _rom)
      set(_anchor "u32 Message = 0xFEEE0000 | (mail & 0xFFFF);")
      string(REGEX MATCHALL "u32 Message = 0xFEEE0000" _matches "${_rom}")
      list(LENGTH _matches _n)
      string(FIND "${_rom}" "${_anchor}" _at)
      if(NOT _n EQUAL 1 OR _at EQUAL -1)
        message(FATAL_ERROR "Pinned DSP ROM HLE contract seam changed")
      endif()
      # Official IROM 66f334fe (Adler32 of host u16 image) echoes the CPU
      # mailbox HIGH half, including its published busy bit. Actual AOT and
      # development-interpreter regressions agree; donor HLE echoed LOW.
      # Whole HLE commands retain raw payloads (Zelda's sync is zero). Only
      # ROM's echo observes the high-half busy bit added by low publication.
      string(REPLACE "${_anchor}" "u32 Message = 0xFEEE8000 | (mail >> 16);" _rom "${_rom}")
      set(_output "${CMAKE_BINARY_DIR}/gcnllr-dsp/adapted/ROM.cpp")
      file(CONFIGURE OUTPUT "${_output}" CONTENT "${_rom}" @ONLY)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_source}")
      list(APPEND _adapted_sources "${_output}")
    else()
      list(APPEND _adapted_sources "${_source}")
    endif()
  endforeach()
  if(NOT _count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one pinned DSP ROM service source")
  endif()
  set_property(TARGET "${target}" PROPERTY SOURCES "${_adapted_sources}")
  set_property(TARGET "${target}" PROPERTY GCNLLR_DSP_ROM_CONTRACT "official-rom-high-status-echo-v2")
endfunction()
