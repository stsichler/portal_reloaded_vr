git submodule update --init --checkout dxvk
pushd .
cd dxvk 
for %%f in (..\dxvk-patches\*) do git am %%f
popd