# 使用方式: make add-lab LAB=lab1 URL=https://github.com/nycuosc2026/lab1-xxx.git
# 預設分支為 main

.PHONY: add-lab

add-lab:
	@if [ -z "$(LAB)" ] || [ -z "$(URL)" ]; then \
		echo "Usage: make add-lab LAB=labX URL=https://github.com/..."; \
		exit 1; \
	fi
	@echo "--- Adding $(LAB) from $(URL) ---"
	
	# 1. 新增遠端並抓取
	git remote add $(LAB)_remote $(URL)
	git fetch $(LAB)_remote
	
	# 2. 建立橋樑分支並切換 (基於遠端的 main)
	git checkout -b $(LAB)_temp_branch $(LAB)_remote/main
	
	# 3. 建立子目錄並移動所有檔案 (包含隱藏檔，排除 .git)
	mkdir -p $(LAB)
	find . -maxdepth 1 ! -name "." ! -name ".git" ! -name "$(LAB)" -exec mv {} $(LAB)/ \;
	
	# 4. Commit 搬移結果
	git add .
	git commit -m "Internal: Move $(LAB) files to $(LAB)/ directory"
	
	# 5. 切換回 main 並合併
	git checkout main
	git merge $(LAB)_temp_branch --allow-unrelated-histories --no-edit
	
	# 6. 清理
	git branch -d $(LAB)_temp_branch
	git remote remove $(LAB)_remote
	
	@echo "--- Successfully integrated $(LAB) ---"