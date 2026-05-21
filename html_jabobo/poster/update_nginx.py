#!/usr/bin/env python3
with open('/etc/nginx/sites-enabled/jabobo', 'r') as f:
    content = f.read()

block = '''    # ===== Jabobo Poster =====
    location /poster/ {
        alias /home/azureuser/tianhao/my_code/Jabobo/jabobo-main/html_jabobo/poster/;
        index index.html;
    }
'''

old = '''    location /test/ {
        alias /home/azureuser/tianhao/my_code/Jabobo/jabobo-main/html_jabobo/;
        autoindex on;
    }'''
new = old + '\n' + block

# Replace all occurrences (both server blocks)
content = content.replace(old, new)

with open('/etc/nginx/sites-enabled/jabobo', 'w') as f:
    f.write(content)

print('Updated nginx config with /poster/ location in both server blocks')
