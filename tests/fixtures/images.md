# 图片显示测试

下面每种图片来源都应当正常显示。

## 1. 本地图片（同目录相对路径）

![本地 logo](img/logo.png)

## 2. 本地图片（`./` 前缀）

![本地 logo 带点](img/logo.png)

## 3. 本地图片（HTML 标签 + 尺寸）

<img src="img/logo.png" width="160" alt="本地 logo">

## 4. 网络图片（Markdown 语法）

![网络 PNG](https://raw.githubusercontent.com/github/explore/main/topics/markdown/markdown.png)

## 5. 网络图片（HTML 标签）

<img src="https://raw.githubusercontent.com/zufuliu/notepad4/main/res/Notepad4.ico" width="64" height="64" alt="网络 ico">

## 6. data: URI 内嵌图片

![内嵌](data:image/gif;base64,R0lGODlhAQABAIAAAAUEBAAAACwAAAAAAQABAAACAkQBADs=)

## 7. 应当被拦截的图片（用于确认白名单生效）

![危险协议](javascript:alert(1))

<img src="file:///C:/Windows/win.ini" alt="越界文件">
