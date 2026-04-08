# ==========================================
# 設定區
# ==========================================
GITHUB_USER := qawl987
ORG_URL     := https://github.com/nycuosc2026
ALL_LABS    := lab1 lab2 lab3 lab4 lab5 lab6 lab7

URL = $(ORG_URL)/$(LAB)-$(GITHUB_USER).git

.PHONY: add sync sync-all help

help:
	@echo "NYCU OS Lab 管理工具 (Git Subtree 穩定版)"
	@echo "用法:"
	@echo "  make add LAB=labX    - 第一次導入某個 Lab"
	@echo "  make sync LAB=labX   - 同步更新某個 Lab"

# 第一次導入：使用 git subtree add
# 這樣 Git 會明確知道「遠端根目錄」對應到「本地子目錄」
add:
	@if [ -z "$(LAB)" ]; then echo "用法: make add LAB=labX"; exit 1; fi
	@echo "--- 正在第一次導入 $(LAB) ---"
	git remote add $(LAB)_remote $(URL) 2>/dev/null || git remote set-url $(LAB)_remote $(URL)
	git fetch $(LAB)_remote
	# git subtree add 會自動建立資料夾並處理所有歷史紀錄
	git subtree add --prefix=$(LAB) $(LAB)_remote main
	@echo "--- $(LAB) 導入成功 ---"

# 同步更新：使用 git subtree pull
# -X theirs 確保衝突時以遠端（Classroom）為準
sync:
	@if [ -z "$(LAB)" ]; then echo "用法: make sync LAB=labX"; exit 1; fi
	@echo "--- 正在同步 $(LAB) 的更新 ---"
	git remote add $(LAB)_remote $(URL) 2>/dev/null || git remote set-url $(LAB)_remote $(URL)
	git fetch $(LAB)_remote
	# git subtree pull 同樣會嚴格鎖定在該子目錄內
	git subtree pull --prefix=$(LAB) $(LAB)_remote main -X theirs --no-edit
	@echo "--- $(LAB) 同步完成 ---"

sync-all:
	@for lab in $(ALL_LABS); do \
		if [ -d "$$lab" ]; then \
			$(MAKE) sync LAB=$$lab; \
		fi \
	done