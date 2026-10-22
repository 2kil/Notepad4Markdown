/* SPDX-License-Identifier: MIT
   Notepad4 Markdown Preview - 页面侧逻辑

   与 C++ 侧的唯一约定：
     window.__mdpreview.reload(version)   通知页面拉取最新正文
     window.__mdpreview.setTheme(mode)    设置主题（auto / light / dark）
   正文本身始终通过 fetch('/__doc') 获取，不通过脚本字符串传递，
   避免文档内容被当作脚本执行。 */

(function () {
	"use strict";

	var THEME_KEY = "mdpreview.theme";
	var currentVersion = -1;
	var lastChars = 0;   // 当前正文 HTML 长度，供状态刷新时复用
	var contentEl = document.getElementById("content");
	var bannerEl = document.getElementById("banner");
	var toggleEl = document.getElementById("theme-toggle");
	var rendering = false;
	var pendingVersion = -1;

	function applyTheme(mode) {
		var root = document.documentElement;
		if (mode === "light" || mode === "dark") {
			root.setAttribute("data-theme", mode);
		} else {
			root.removeAttribute("data-theme");
			mode = "auto";
		}
		try {
			localStorage.setItem(THEME_KEY, mode);
		} catch (e) {
			/* localStorage 不可用时忽略 */
		}
		if (toggleEl) {
			toggleEl.textContent = mode === "light" ? "☀" : (mode === "dark" ? "☾" : "◐");
			toggleEl.title = "当前主题：" + (mode === "auto" ? "跟随系统" : (mode === "light" ? "浅色" : "深色"));
		}
		return mode;
	}

	function loadTheme() {
		var saved = "auto";
		try {
			saved = localStorage.getItem(THEME_KEY) || "auto";
		} catch (e) {
			saved = "auto";
		}
		return applyTheme(saved);
	}

	function nextTheme(mode) {
		if (mode === "auto") { return "light"; }
		if (mode === "light") { return "dark"; }
		return "auto";
	}

	function showBanner(text) {
		if (!bannerEl) { return; }
		if (text) {
			bannerEl.textContent = text;
			bannerEl.hidden = false;
		} else {
			bannerEl.hidden = true;
			bannerEl.textContent = "";
		}
	}

	function highlightAll() {
		if (typeof window.hljs === "undefined" || !contentEl) {
			return;
		}
		var blocks = contentEl.querySelectorAll("pre > code");
		// 超长文档限制高亮总量，避免一次性消耗过多 CPU 造成滚动卡顿。
		var budget = 400 * 1024;
		for (var i = 0; i < blocks.length; i++) {
			var text = blocks[i].textContent || "";
			if (budget <= 0) {
				break;
			}
			budget -= text.length;
			if (text.length > 200 * 1024) {
				continue;
			}
			try {
				window.hljs.highlightElement(blocks[i]);
			} catch (e) {
				/* 单个代码块高亮失败不影响整体渲染 */
			}
		}
	}

	function attachImageFallback() {
		if (!contentEl) { return; }
		var images = contentEl.querySelectorAll("img");
		for (var i = 0; i < images.length; i++) {
			images[i].addEventListener("error", function () {
				this.setAttribute("data-broken", "1");
				this.title = "图片无法加载：" + (this.getAttribute("src") || "");
				notifyHost(null);
			});
			// 本地图片通常是瞬间完成，远程图片要等下载；两者都在此上报，
			// 让宿主能知道"图真的显示出来了"（自动化测试也依赖这个）。
			images[i].addEventListener("load", function () {
				notifyHost(null);
			});
		}
	}

	// 统计当前图片的加载状态（不读取图片内容，只读 DOM 状态）。
	function imageStats() {
		var stats = { total: 0, loaded: 0, failed: 0 };
		if (!contentEl) { return stats; }
		var images = contentEl.querySelectorAll("img");
		stats.total = images.length;
		for (var i = 0; i < images.length; i++) {
			var img = images[i];
			if (!img.getAttribute("src") || img.getAttribute("data-blocked")) {
				continue;   // 被安全策略替换掉的图片不计入
			}
			if (img.complete) {
				if (img.naturalWidth > 0) {
					stats.loaded += 1;
				} else {
					stats.failed += 1;
				}
			}
		}
		return stats;
	}

	function applyDoc(html) {
		if (!contentEl) { return; }
		lastChars = html ? html.length : 0;
		var doc = document.documentElement;
		var maxScroll = doc.scrollHeight - window.innerHeight;
		var ratio = maxScroll > 0 ? window.scrollY / maxScroll : 0;

		contentEl.innerHTML = html;

		var newMax = doc.scrollHeight - window.innerHeight;
		if (newMax > 0 && ratio > 0) {
			window.scrollTo(0, Math.min(newMax, Math.round(ratio * newMax)));
		}

		highlightAll();
		attachImageFallback();
		notifyHost(html);
	}

	// 页内锚点：目录里的 [文字](#标题) 要跳到对应标题。
	// 自己处理点击而不是依赖浏览器默认行为——内容是通过 innerHTML 注入的，
	// 并且需要在找不到 id 时按文本再兜底匹配一次。
	function decodeHash(hash) {
		if (!hash) { return ""; }
		try {
			return decodeURIComponent(hash);
		} catch (e) {
			return hash;
		}
	}

	function findAnchorTarget(hash) {
		var name = decodeHash(hash.replace(/^#/, ""));
		if (!name) { return null; }

		var direct = document.getElementById(name);
		if (direct) { return direct; }
		// 兼容大小写差异（作者手写目录时常见）
		direct = document.getElementById(name.toLowerCase());
		if (direct) { return direct; }

		// 兜底：把标题文本按同样规则归一化后比较，避免作者手写锚点与
		// GitHub 的 slug 规则有细微出入时点了没反应。
		function normalize(text) {
			return (text || "").toLowerCase()
				.replace(/[\s\u00a0]+/g, "-")
				.replace(/[!"#$%&'()*+,.\/:;<=>?@\[\\\]^`{|}~]/g, "")
				.replace(/[\u2000-\u206f\u2e00-\u2e7f\u3000-\u303f\ufe30-\ufe6f\uff01-\uff65]/g, "")
				.replace(/-+/g, "-")
				.replace(/^-|-$/g, "");
		}
		var wanted = normalize(name);
		if (!wanted) { return null; }
		var headings = contentEl ? contentEl.querySelectorAll("h1,h2,h3,h4,h5,h6") : [];
		for (var i = 0; i < headings.length; i++) {
			if (normalize(headings[i].textContent) === wanted) {
				return headings[i];
			}
		}
		return null;
	}

	function scrollToAnchor(hash, updateLocation) {
		var target = findAnchorTarget(hash);
		if (!target) { return false; }
		target.scrollIntoView({ block: "start" });
		// 给目标一点视觉反馈，避免"点完看不出跳到了哪里"。
		target.classList.remove("anchor-flash");
		void target.offsetWidth;
		target.classList.add("anchor-flash");
		if (updateLocation) {
			try {
				history.replaceState(null, "", hash);
			} catch (e) {
				/* 快照在 file:// 下可能禁止改历史，忽略即可 */
			}
		}
		return true;
	}

	if (contentEl) {
		contentEl.addEventListener("click", function (event) {
			var node = event.target;
			while (node && node !== contentEl && node.tagName !== "A") {
				node = node.parentNode;
			}
			if (!node || node === contentEl || node.tagName !== "A") { return; }
			var href = node.getAttribute("href") || "";
			if (href.charAt(0) !== "#" || href === "#" || href === "#md-blocked-link") {
				return;
			}
			event.preventDefault();
			scrollToAnchor(href, true);
		});
	}

	window.addEventListener("hashchange", function () {
		scrollToAnchor(window.location.hash, false);
	});

	// 渲染完成后通知宿主：只传长度、版本与图片统计，不传正文。
	function notifyHost(html) {
		try {
			if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
				window.chrome.webview.postMessage({
					type: "rendered",
					version: currentVersion,
					chars: html ? html.length : lastChars,
					images: imageStats()
				});
			}
		} catch (e) {
			/* 非 WebView2 环境（例如浏览器快照）忽略 */
		}
	}

	async function refreshMeta() {
		try {
			var response = await fetch("/__meta", { cache: "no-store" });
			if (!response.ok) { return; }
			var meta = await response.json();
			document.title = meta.title ? (meta.title + (meta.modified ? " *" : "") + " - Markdown 预览") : "Markdown 预览";
			showBanner(meta.error || "");
		} catch (e) {
			/* 元数据失败不影响正文 */
		}
	}

	async function reload(version) {
		if (typeof version === "number" && version === currentVersion) {
			return;
		}
		// 迟到的旧版本请求直接丢弃（C++ 侧也可能因重启而回退版本号）。
		if (typeof version === "number" && version < currentVersion) {
			return;
		}
		if (rendering) {
			pendingVersion = (typeof version === "number") ? version : pendingVersion;
			return;
		}
		rendering = true;
		try {
			var query = "/__doc?ts=" + Date.now();
			var response = await fetch(query, { cache: "no-store" });
			var html = await response.text();
			if (typeof version === "number") {
				currentVersion = version;
			}
			applyDoc(html);
			await refreshMeta();
		} catch (e) {
			showBanner("预览更新失败：" + e);
		} finally {
			rendering = false;
			if (pendingVersion >= 0 && pendingVersion !== currentVersion) {
				var next = pendingVersion;
				pendingVersion = -1;
				reload(next);
			}
		}
	}

	window.__mdpreview = {
		reload: reload,
		setTheme: function (mode) { applyTheme(mode); },
		refreshMeta: refreshMeta
	};

	if (toggleEl) {
		var mode = loadTheme();
		toggleEl.addEventListener("click", function () {
			mode = nextTheme(mode);
			applyTheme(mode);
		});
	} else {
		loadTheme();
	}

	if (document.readyState === "loading") {
		document.addEventListener("DOMContentLoaded", function () { reload(0); });
	} else {
		// 脚本被延迟注入时 DOMContentLoaded 已经过去，直接拉取。
		reload(0);
	}
})();
