# DashCore — Android rendering optimization

## 0.9.9-alpha — GameObject bounds fallback

- AutoTest 214 showed all 122 eligible Stereo Madness GameObjects had empty/invalid CCNode content sizes, preventing the previous bounds calculation from classifying candidates.
- Uses GameObject::getBoundingRect() as a fallback when content size is invalid, transforming all four parent-relative corners through the parent node.
- Reports fallback and invalid-rectangle counts. Any invalid or uncertain bounds remain on vanilla rendering.
- Keeps the 125% guard band and experimental culling OFF by default.
- This diagnostic version is not a validated optimization; cloud emulator results do not replace testing on the physical Huawei Y9s / Mali-G51.


