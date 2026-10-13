Import("env")

import os

project_dir = env.subst("$PROJECT_DIR")
build_dir = env.subst("$BUILD_DIR")

lwip_source_dir = os.path.join(
    project_dir,
    "Middlewares",
    "Third_Party",
    "LwIP",
    "src"
)

lwip_build_dir = os.path.join(
    build_dir,
    "lwip"
)

env.BuildSources(
    lwip_build_dir,
    lwip_source_dir
)