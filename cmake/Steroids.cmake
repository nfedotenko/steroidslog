include(FetchContent)

FetchContent_Declare(
    steroids
    GIT_REPOSITORY https://github.com/nfedotenko/steroids.git
    GIT_TAG 9c114de11856
)

FetchContent_MakeAvailable(steroids)
