install(
    TARGETS strella_exe
    RUNTIME COMPONENT strella_Runtime
)

if(PROJECT_IS_TOP_LEVEL)
  include(CPack)
endif()
