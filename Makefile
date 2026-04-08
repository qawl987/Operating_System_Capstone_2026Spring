# ==========================================
# 設定區
# ==========================================
GITHUB_USER := qawl987
ORG_URL     := https://github.com/nycuosc2026
ALL_LABS    := lab1 lab2 lab3 lab4 lab5 lab6 lab7

URL = $(ORG_URL)/$(LAB)-$(GITHUB_USER).git

.PHONY: add sync sync-all help push-all

help:
	@echo "NYCU OS Lab 管理工具 (穩定修正版)"
	@echo "用法:"
	@echo "  make add LAB=labX    - 第一次導入某個 Lab"
	@echo "  make sync LAB=labX   - 同步更新並強制覆蓋 (以遠端為主)"
	@echo "  make sync-all        - 同步所有已存在的 Labs 並推送到個人 GitHub"

# [Step 1] 第一次導入：依然使用 git subtree add (最安全)
add:
	@if [ -z "$(LAB)" ]; then echo "用法: make add LAB=labX"; exit 1; fi
	@echo "--- 正在第一次導入 $(LAB) ---"
	git remote add $(LAB)_remote $(URL) 2>/dev/null || git remote set-url $(LAB)_remote $(URL)
	git fetch $(LAB)_remote
	git subtree add --prefix=$(LAB) $(LAB)_remote main
	@echo "--- $(LAB) 導入成功 ---"

# [Step 2] 同步更新：拆解 pull 動作，改用 merge 以支援 -X theirs
sync:
	@if [ -z "$(LAB)" ]; then echo "用法: make sync LAB=labX"; exit 1; fi
	@echo "--- 正在同步 $(LAB) 的更新 (強制覆蓋模式) ---"
	git remote add $(LAB)_remote $(URL) 2>/dev/null || git remote set-url $(LAB)_remote $(URL)
	git fetch $(LAB)_remote
	# 使用標準 merge 指令，這就能吃 -X theirs 了
	# -X subtree=$(LAB)/ 確保 Git 知道要合併到哪個目錄
	git merge $(LAB)_remote/main --allow-unrelated-histories -X theirs -X subtree=$(LAB)/ --no-edit
	@echo "--- $(LAB) 同步完成 ---"

# [Step 3] 一次同步所有 + 推送備份
sync-all:
	@for lab in $(ALL_LABS); do \
		if [ -d "$$lab" ]; then \
			$(MAKE) sync LAB=$$lab; \
		fi \
	done