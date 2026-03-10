set(LLVM_LINK_COMPONENTS
  JsonCheck
  Support
)

add_llvm_unittest(JsonCheckTests
  JsonParserTest.cpp
)
