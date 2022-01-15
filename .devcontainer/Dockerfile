# See here for image contents: https://github.com/microsoft/vscode-dev-containers/tree/v0.209.6/containers/ubuntu/.devcontainer/base.Dockerfile

# [Choice] Ubuntu version (use hirsuite or bionic on local arm64/Apple Silicon): hirsute, focal, bionic
ARG VARIANT="hirsute"
# VS Code Remote / GitHub Codespaces Development Container Images: https://github.com/microsoft/vscode-dev-containers/tree/main/containers/cpp:
FROM mcr.microsoft.com/vscode/devcontainers/cpp:0-${VARIANT}

# TODO: Add a nonroot user into sudoers: <--
# https://code.visualstudio.com/remote/advancedcontainers/add-nonroot-user:
ARG USERNAME=root
# ARG USER_UID=1000
# ARG USER_GID=$USER_UID
# # HB1: TODO: Add a nonroot user into sudoers: -->

# [Optional] Uncomment this section to install additional OS packages.
# RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
#     && apt-get -y install --no-install-recommends \
#            <your-package-list-here>
