include(FetchContent)

# Single fmt for everyone
FetchContent_Declare(fmtlib
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 11.1.2)
FetchContent_MakeAvailable(fmtlib)

# spdlog: use external fmt
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Use external fmt" FORCE)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3)
FetchContent_MakeAvailable(spdlog)

# quill
FetchContent_Declare(quill
  GIT_REPOSITORY https://github.com/odygrd/quill.git
  GIT_TAG v10.0.1)
FetchContent_MakeAvailable(quill)

# fmtlog: fetch sources ONLY
FetchContent_Declare(fmtlog
  GIT_REPOSITORY https://github.com/MengRao/fmtlog.git
  GIT_TAG v2.3.0)
FetchContent_GetProperties(fmtlog)
if(NOT fmtlog_POPULATED)
  FetchContent_Populate(fmtlog)  # get fmtlog.h
endif()
