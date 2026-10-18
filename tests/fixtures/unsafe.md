# 安全测试

<script>alert('xss')</script>

<img src=x onerror="alert(1)">

[危险链接](javascript:alert(1))

[控制字符链接](java\tscript:alert(2))

[协议相对](//evil.example.com/x)

![危险图片](javascript:alert(3))

![data 图片](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==)

<style>body{display:none}</style>

<iframe src="https://example.com"></iframe>
