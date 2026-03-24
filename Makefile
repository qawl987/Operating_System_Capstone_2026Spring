# ==========================================
# 設定區：請確認你的 GitHub ID 是否正確
# ==========================================
GITHUB_USER := qawl987
ORG_URL     := https://github.com/nycuosc2026
ALL_LABS    := lab1 lab2 lab3 lab4 lab5 lab6 lab7

# ==========================================
# 自動生成 URL 邏輯
# ==========================================
# 如果有傳入 LAB=lab1，URL 就會是 https://github.com/nycuosc2026/lab1-qawl987.git
URL = $(ORG_URL)/$(LAB)-$(GITHUB_USER).git

.PHONY: add sync sync-all help

help:
	@echo "NYCU OS Lab Sync Tool"
	@echo "Usage:"
	@echo "  make add LAB=labX    - 第一次導入某個 Lab (例如 make add LAB=lab3)"
	@echo "  make sync LAB=labX   - 同步更新某個 Lab (例如 make sync LAB=lab2)"
	@echo "  make sync-all        - 一次更新所有已經導入過的 Labs"

# 第一次導入
add:
	@if [ -z "$(LAB)" ]; then echo "Usage: make add LAB=labX"; exit 1; fi
	@echo "--- Initializing $(LAB) from $(URL) ---"
	git remote add $(LAB)_remote $(URL) 2>/dev/null || git remote set-url $(LAB)_remote $(URL)
	git fetch $(LAB)_remote
	git checkout -b $(LAB)_init_temp $(LAB)_remote/main
	mkdir -p $(LAB)
	find . -maxdepth 1 ! -name "." ! -name ".git" ! -name "$(LAB)" -exec mv {} $(LAB)/ \;
	git add .
	git commit -m "Internal: Initialize $(LAB) folder structure"
	git checkout main
	git merge $(LAB)_init_temp --allow-unrelated-histories --no-edit
	git branch -D $(LAB)_init_temp
	@echo "--- $(LAB) added! ---"

# 無腦同步更新
sync:
	@if [ -z "$(LAB)" ]; then echo "Usage: make sync LAB=labX"; exit 1; fi
	@echo "--- Syncing $(LAB) from $(URL) ---"
	git remote add $(LAB)_remote $(URL) 2>/dev/null || git remote set-url $(LAB)_remote $(URL)
	git fetch $(LAB)_remote
	git branch -D $(LAB)_sync_temp 2>/dev/null || true
	git checkout -b $(LAB)_sync_temp $(LAB)_remote/main
	mkdir -p $(LAB)
	find . -maxdepth 1 ! -name "." ! -name ".git" ! -name "$(LAB)" -exec mv {} $(LAB)/ \;
	git add .
	git commit -m "Internal: Prepare $(LAB) for sync" || echo "No changes"
	git checkout main
	# 使用 -X theirs 強制以遠端內容為準
	git merge $(LAB)_sync_temp --allow-unrelated-histories -X theirs --no-edit
	git branch -D $(LAB)_sync_temp
	@echo "--- $(LAB) sync complete ---"

# 一次更新所有 Lab (會跳過尚未 add 的 lab)
sync-all:
	@for lab in $(ALL_LABS); do \
		if [ -d "$$lab" ]; then \
			$(MAKE) sync LAB=$$lab; \
		fi \
	done