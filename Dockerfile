FROM mcr.microsoft.com/windows/servercore:ltsc2022 AS base
SHELL ["cmd", "/S", "/C"]

RUN powershell -NoProfile -Command "Set-ExecutionPolicy Bypass -Scope Process -Force; \
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; \
    iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1')); \
    choco install curl git 7zip -y; \
    choco install cmake --installargs 'ADD_CMAKE_TO_PATH=System' -y"

RUN \
    curl -SL --output vs_buildtools.exe https://aka.ms/vs/17/release/vs_buildtools.exe \
    && (start /w vs_buildtools.exe --quiet --wait --norestart --nocache \
        --installPath "%ProgramFiles(x86)%\\Microsoft Visual Studio\\2022\\BuildTools" \
        --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
        --add Microsoft.VisualStudio.Component.Windows11SDK.26100 \
        --remove Microsoft.VisualStudio.Component.Windows10SDK.10240 \
        --remove Microsoft.VisualStudio.Component.Windows10SDK.10586 \
        --remove Microsoft.VisualStudio.Component.Windows10SDK.14393 \
        --remove Microsoft.VisualStudio.Component.Windows81SDK \
        || IF "%ERRORLEVEL%"=="3010" EXIT 0) \
    && del /q vs_buildtools.exe

RUN git clone https://github.com/microsoft/vcpkg.git C:/vcpkg && \
    cd C:/vcpkg && \
    bootstrap-vcpkg.bat

RUN setx /M VCPKG_ROOT "C:/vcpkg" && mkdir C:\skyrim-community-shaders

WORKDIR C:/skyrim-community-shaders

ENTRYPOINT ["powershell", "-File", "C:/skyrim-community-shaders/containerbuild.ps1"]
