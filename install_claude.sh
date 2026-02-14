#!/bin/bash

set -euo pipefail

# ========================
#       常量定义
# ========================
SCRIPT_NAME=$(basename "$0")
NODE_MIN_VERSION=18
NODE_INSTALL_VERSION=22
NVM_VERSION="v0.40.3"
CLAUDE_PACKAGE="@anthropic-ai/claude-code"
CONFIG_DIR="$HOME/.claude"
CONFIG_FILE="$CONFIG_DIR/settings.json"
API_BASE_URL="https://open.bigmodel.cn/api/anthropic"
API_KEY_URL="https://open.bigmodel.cn/usercenter/proj-mgmt/apikeys"
API_TIMEOUT_MS=3000000

# 国内镜像源配置
NVM_GIT_REPO="https://gitee.com/mirrors/nvm.git"
NODE_MIRROR="https://npmmirror.com/mirrors/node"
NPM_REGISTRY="https://registry.npmmirror.com"

# ========================
#       工具函数
# ========================

log_info() { echo "🔹 $*"; }
log_success() { echo "✅ $*"; }
log_error() { echo "❌ $*" >&2; }

ensure_dir_exists() {
    local dir="$1"
    if [ ! -d "$dir" ]; then
        mkdir -p "$dir" || { log_error "Failed to create directory: $dir"; exit 1; }
    fi
}

# ========================
#     Node.js 安装函数
# ========================

install_nodejs() {
    local platform=$(uname -s)

    case "$platform" in
        Linux|Darwin)
            log_info "Installing Node.js on $platform (using China mirrors)..."

            # 1. 安装/配置 nvm (使用 Gitee 镜像)
            export NVM_DIR="$HOME/.nvm"
            if [ ! -d "$NVM_DIR" ]; then
                log_info "Cloning nvm from Gitee..."
                git clone "$NVM_GIT_REPO" "$NVM_DIR"
                cd "$NVM_DIR" && git checkout `git describe --abbrev=0 --tags --match "v[0-9]*" $(git rev-list --tags --max-count=1)`
                cd - > /dev/null
            fi

            # 加载 nvm 并在此时注入 Node 镜像环境变量
            log_info "Loading nvm environment..."
            [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
            export NVM_NODEJS_ORG_MIRROR="$NODE_MIRROR"

            # 2. 安装 Node.js
            log_info "Installing Node.js $NODE_INSTALL_VERSION via npmmirror..."
            nvm install "$NODE_INSTALL_VERSION"
            nvm use "$NODE_INSTALL_VERSION"

            # 3. 配置 npm 镜像
            log_info "Setting npm registry to $NPM_REGISTRY..."
            npm config set registry "$NPM_REGISTRY"

            # 验证安装
            node -v &>/dev/null || { log_error "Node.js installation failed"; exit 1; }
            log_success "Node.js installed: $(node -v)"
            log_success "npm registry: $(npm config get registry)"
            ;;
        *)
            log_error "Unsupported platform: $platform"
            exit 1
            ;;
    esac
}

# ========================
#     Node.js 检查函数
# ========================

check_nodejs() {
    # 即使已安装 node，也需要确保加载了 nvm 变量（如果是脚本重复运行）
    [ -s "$HOME/.nvm/nvm.sh" ] && \. "$HOME/.nvm/nvm.sh"

    if command -v node &>/dev/null; then
        current_version=$(node -v | sed 's/v//')
        major_version=$(echo "$current_version" | cut -d. -f1)

        if [ "$major_version" -ge "$NODE_MIN_VERSION" ]; then
            log_success "Node.js is already installed: v$current_version"
            # 确保即使已安装，镜像也是对的
            npm config set registry "$NPM_REGISTRY"
            return 0
        else
            log_info "Node.js v$current_version is installed but version < $NODE_MIN_VERSION. Upgrading..."
            install_nodejs
        fi
    else
        log_info "Node.js not found. Installing..."
        install_nodejs
    fi
}

# ========================
#     Claude Code 安装
# ========================

install_claude_code() {
    # 强制在安装前再次确认镜像源，防止被系统默认设置覆盖
    npm config set registry "$NPM_REGISTRY"
    
    if command -v claude &>/dev/null; then
        log_success "Claude Code is already installed: $(claude --version)"
    else
        log_info "Installing Claude Code from $NPM_REGISTRY..."
        npm install -g "$CLAUDE_PACKAGE" || {
            log_error "Failed to install claude-code"
            exit 1
        }
        log_success "Claude Code installed successfully"
    fi
}

configure_claude_json(){
  log_info "Initializing .claude.json..."
  node --eval '
      const os = require("os");
      const fs = require("fs");
      const path = require("path");

      const homeDir = os.homedir();
      const filePath = path.join(homeDir, ".claude.json");
      try {
          const content = fs.existsSync(filePath) ? JSON.parse(fs.readFileSync(filePath, "utf-8")) : {};
          fs.writeFileSync(filePath, JSON.stringify({ ...content, hasCompletedOnboarding: true }, null, 2), "utf-8");
      } catch (e) {
          fs.writeFileSync(filePath, JSON.stringify({ hasCompletedOnboarding: true }, null, 2), "utf-8");
      }'
}

# ========================
#     API Key 配置
# ========================

configure_claude() {
    log_info "Configuring Claude Code settings..."
    echo "   You can get your API key from: $API_KEY_URL"
    read -s -p "🔑 Please enter your ZHIPU API key: " api_key
    echo

    if [ -z "$api_key" ]; then
        log_error "API key cannot be empty. Please run the script again."
        exit 1
    fi

    ensure_dir_exists "$CONFIG_DIR"

    # 写入配置文件
    node --eval '
        const os = require("os");
        const fs = require("fs");
        const path = require("path");

        const homeDir = os.homedir();
        const filePath = path.join(homeDir, ".claude", "settings.json");
        const apiKey = "'"$api_key"'";

        const content = fs.existsSync(filePath)
            ? JSON.parse(fs.readFileSync(filePath, "utf-8"))
            : {};

        fs.writeFileSync(filePath, JSON.stringify({
            ...content,
            env: {
                ANTHROPIC_AUTH_TOKEN: apiKey,
                ANTHROPIC_BASE_URL: "'"$API_BASE_URL"'",
                API_TIMEOUT_MS: '"$API_TIMEOUT_MS"',
                CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC: "1"
            }
        }, null, 2), "utf-8");
    ' || {
        log_error "Failed to write settings.json"
        exit 1
    }

    log_success "Claude Code configured successfully"
}

# ========================
#         主流程
# ========================

main() {
    echo "🚀 Starting $SCRIPT_NAME with China-optimized mirrors"

    check_nodejs
    install_claude_code
    configure_claude_json
    configure_claude

    # 将 NVM 镜像持久化到 bashrc
    if ! grep -q "NVM_NODEJS_ORG_MIRROR" "$HOME/.bashrc"; then
        echo "export NVM_NODEJS_ORG_MIRROR=$NODE_MIRROR" >> "$HOME/.bashrc"
    fi

    echo ""
    log_success "🎉 Installation completed successfully!"
    echo ""
    echo "🚀 Please run this command to refresh your shell:"
    echo "   source ~/.bashrc"
    echo ""
    echo "🚀 Then start Claude Code by typing:"
    echo "   claude"
}

main "$@"