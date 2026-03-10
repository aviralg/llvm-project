add_llvm_component_library(LLVMJsonCheck
  JsonParser.cpp

  ADDITIONAL_HEADER_DIRS
  "${LLVM_MAIN_INCLUDE_DIR}/llvm/JsonCheck"
)

target_link_libraries(LLVMJsonCheck LLVMSupport)
