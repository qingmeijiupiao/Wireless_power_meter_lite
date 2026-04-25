# 接收从 devcontainer.json 传递过来的版本参数
ARG IDF_VERSION=latest
# 基于乐鑫官方的 IDF Docker 镜像构建，并切换到指定版本标签
FROM espressif/idf:${IDF_VERSION}

# 设置容器启动后的 shell 环境，自动加载 IDF 的环境变量
RUN echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> ~/.bashrc

# 保持与官方 Docker 镜像一致的入口点
ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]
CMD ["/bin/bash"]