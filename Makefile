# ==========================================
# 設定區
# ==========================================
GITHUB_USER := qawl987
ORG_URL     := https://github.com/nycuosc2026
ALL_LABS    := lab1 lab2 lab3 lab4 lab5 lab6 lab7

URL = $(ORG_URL)/$(LAB)-$(GITHUB_USER).git

.PHONY: add sync sync-all help

help:
	@echo "NYCU OS Lab 管理工具 (Subtree Merge 版)"
	@echo "用法: make add LAB=lab1 | make sync LAB=lab1 | make sync-all"

# 第一次導入與後續更新，現在邏輯統一了
# 使用 -X subtree=路徑/ 可以自動處理目錄映射，避免 rename conflict
add sync:
	@if [ -z "$(LAB)" ]; then echo "用法: make $@ LAB=labX"; exit 1; fi
	@echo "--- 正在處理 $(LAB) ---"
	# 1. 確保遠端連結存在
	git remote add $(LAB)_remote $(URL) 2>/dev/null || git remote set-url $(LAB)_remote $(URL)
	git fetch $(LAB)_remote
	# 2. 直接合併
	# --allow-unrelated-histories: 允許合併沒有共同祖先的 repo
	# -X theirs: 發生衝突時，以遠端內容為準
	# -X subtree=$(LAB)/: 重要！自動將遠端根目錄映射到本地子目錄
	git merge $(LAB)_remote/main --allow-unrelated-histories -X theirs -X subtree=$(LAB)/ --no-edit
	@echo "--- $(LAB) 處理完成 ---"

sync-all:
	@for lab in $(ALL_LABS); do \
		if [ -d "$$lab" ]; then \
			$(MAKE) sync LAB=$$lab; \
		fi \
	done