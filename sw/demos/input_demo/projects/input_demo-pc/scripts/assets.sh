#!/bin/bash
echo "----------------------------"
echo "Building assets"
echo "----------------------------"

cd ..
cd ..

python3 deps/sdk/tools/resource_packer/resource_packer.py -f "assets/assets.json" -o "src/resources/"