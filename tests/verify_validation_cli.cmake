if(NOT DEFINED CLI OR NOT DEFINED OUTPUT_DIR)
  message(FATAL_ERROR "CLI and OUTPUT_DIR are required")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
# A complete signature used to pass the framing-only inspection.
set(input "${OUTPUT_DIR}/signature-only.zjs")
file(WRITE "${input}" "JZJZ")
foreach(command validate usb-print)
  execute_process(COMMAND "${CLI}" "${command}" "${input}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
  if(NOT result STREQUAL "1" OR NOT error MATCHES "Incomplete ZJS print document")
    message(FATAL_ERROR "${command} did not reject the document before device access: ${result} ${output} ${error}")
  endif()
endforeach()
