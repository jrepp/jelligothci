# Changelog

## [0.5.0](https://github.com/jrepp/jelligothci/compare/v0.4.0...v0.5.0) (2026-10-10)


### Features

* add stimulus-driven creature behavior and cleanup ([#14](https://github.com/jrepp/jelligothci/issues/14)) ([375ea79](https://github.com/jrepp/jelligothci/commit/375ea79c4f290fe45b391d751d71830693b62ff5))
* report behaviour, potty and mess in the debug state and viewer ([fe5157a](https://github.com/jrepp/jelligothci/commit/fe5157aa055512fab5e858cd99f0c6b371ef8eec))


### Bug Fixes

* expire offline behavior without unseen accidents ([42cff9f](https://github.com/jrepp/jelligothci/commit/42cff9f029e4a3dbc5213b070aa2833d277ed050))
* keep care interruptions and deferred requests playable ([710197e](https://github.com/jrepp/jelligothci/commit/710197e1a8217196e6a8e94a9f1b1428753429e4))
* keep care interruptions and deferred requests playable ([1b5d726](https://github.com/jrepp/jelligothci/commit/1b5d726ac30944ec1168627798958328e1365294))
* keep care interruptions and deferred requests playable ([#17](https://github.com/jrepp/jelligothci/issues/17)) ([710197e](https://github.com/jrepp/jelligothci/commit/710197e1a8217196e6a8e94a9f1b1428753429e4))
* render the first SDL frame before serving debug clients ([2471db9](https://github.com/jrepp/jelligothci/commit/2471db99a3d404a32a50e83b72b38102856718ec))

## [0.4.0](https://github.com/jrepp/jelligothci/compare/v0.3.0...v0.4.0) (2026-10-10)


### Features

* add creature content, Reading and potty routines ([#10](https://github.com/jrepp/jelligothci/issues/10)) ([fdcdbf7](https://github.com/jrepp/jelligothci/commit/fdcdbf79414713daf1b37773c492eae7e3f85bc1))
* add data-driven Reading and potty routines ([fdcdbf7](https://github.com/jrepp/jelligothci/commit/fdcdbf79414713daf1b37773c492eae7e3f85bc1))
* add localized brushing and bath bubbles ([588286b](https://github.com/jrepp/jelligothci/commit/588286bbe096a2424da32509feee9c3ba892e918))
* add persistent volume controls and louder defaults ([3d9f6ea](https://github.com/jrepp/jelligothci/commit/3d9f6ea463f20d8a20e9ff130dd06aa151e68622))
* add Reading and a natural potty cycle with activities as data ([428f697](https://github.com/jrepp/jelligothci/commit/428f6971c9ad5a55bc67a443e1862de49997175a))
* add the axolotl species with data-driven evolution sets and clips ([15f590c](https://github.com/jrepp/jelligothci/commit/15f590c7b30ad94f4f4f55b471a3d5dff447d9bf))
* **assets:** add placeholder book icon and prop for a Reading activity ([675fae3](https://github.com/jrepp/jelligothci/commit/675fae3529364f10d9d3af277418704b9616dc64))
* **assets:** add the surprised axolotl frame as its curious pose ([d746eeb](https://github.com/jrepp/jelligothci/commit/d746eeb52ea5b5105c6081501ca7cc5ba97e73ea))
* **assets:** import axolotl frames and author per-pose creature clips ([2cf2c6c](https://github.com/jrepp/jelligothci/commit/2cf2c6cbd04c372b91de85dfbca03b54afba7ea5))
* diagnose ESP32 framebuffer and panel mismatches ([5aef895](https://github.com/jrepp/jelligothci/commit/5aef895c5a4ec397cc19d6b1e029c9bb0b663a0b))
* distinguish menu and pet interaction feedback ([57aa67a](https://github.com/jrepp/jelligothci/commit/57aa67a5a66bba61affb841ad47bc152bc7484a6))
* drive creature behaviour and render scale from content/creatures.json ([1dfcebf](https://github.com/jrepp/jelligothci/commit/1dfcebf040187049a18ce202e14cfcb550c1bfa0))
* improve pet care and ESP32 display reliability ([#8](https://github.com/jrepp/jelligothci/issues/8)) ([1c9692c](https://github.com/jrepp/jelligothci/commit/1c9692cde70f3878447441a24a4bdc465bc3975c))
* **jelli-art:** author and preview creature clips ([a9da182](https://github.com/jrepp/jelligothci/commit/a9da1821fa7e1e6fa63b22443e8874a3af2a0017))
* **jelli-art:** author creature behaviour and render profiles ([b9c4fe0](https://github.com/jrepp/jelligothci/commit/b9c4fe062eb9a31123f8429747dde4b48ab8d80f))
* share pet slots across selectable unlocked evolutions ([131bfc9](https://github.com/jrepp/jelligothci/commit/131bfc980934ae46aa3560f7e112be4d2ae8a662))
* wake pets by touch with rest-based moods and bonding ([4b87655](https://github.com/jrepp/jelligothci/commit/4b8765569be3cfd34348f707e730d74f71850cbb))


### Bug Fixes

* **assets:** re-import the corrected surprised axolotl frame ([808dec6](https://github.com/jrepp/jelligothci/commit/808dec6c40ce72b1bfe9d18343ed43418aa81193))
* declare CMake policy baseline in catalog script tests ([0cfaf7c](https://github.com/jrepp/jelligothci/commit/0cfaf7c9c8ffb74a9718669993eb8a93c2d5b364))
* explain basic care and show recovery progress ([be776e1](https://github.com/jrepp/jelligothci/commit/be776e16234fb9ef62a63f8e34e6f3152f326548))
* keep activity input assertions warning-clean on ESP32 ([dc81a99](https://github.com/jrepp/jelligothci/commit/dc81a99b6acb258522b33d7781f55b7cc7c9b801))
* label menu actions and explain unavailable controls ([7de3d63](https://github.com/jrepp/jelligothci/commit/7de3d63394af4e8199e81efcdc8bc15d3c1cddd6))
* make food readable and refillable from its menu ([b1c635a](https://github.com/jrepp/jelligothci/commit/b1c635afe36d0382ae7bb0629e54319468b7ce96))
* normalize asset manifest paths on Windows ([e3c0c4e](https://github.com/jrepp/jelligothci/commit/e3c0c4e92b7fc3414b6032571110608468708155))
* reserve DMA storage for ESP32 panel transfers ([7a87b3b](https://github.com/jrepp/jelligothci/commit/7a87b3b6653457eab3a75b036020caf0ff1b24ee))


### Performance Improvements

* clip rectangle fills once per scanline ([cd8f999](https://github.com/jrepp/jelligothci/commit/cd8f999f4471826a5df944f392d183683c2a38b4))
* clip rectangle fills once per scanline ([a152e05](https://github.com/jrepp/jelligothci/commit/a152e05f2940d72d635d81ccc518750d20c9f592))
* clip rectangle fills once per scanline ([#12](https://github.com/jrepp/jelligothci/issues/12)) ([cd8f999](https://github.com/jrepp/jelligothci/commit/cd8f999f4471826a5df944f392d183683c2a38b4))

## [0.3.0](https://github.com/jrepp/jelligothci/compare/v0.2.0...v0.3.0) (2026-10-10)


### Features

* add authored meal fruit and soup choices ([0edea6a](https://github.com/jrepp/jelligothci/commit/0edea6a81f11183364ca96858b89764a5b9b7dc6))
* add authored pet collections and persistent unlocks ([9786862](https://github.com/jrepp/jelligothci/commit/9786862c44b3eca1d117249cead0c13c758f1ed1))
* add barbell workouts with food and hydration costs ([f79f7ba](https://github.com/jrepp/jelligothci/commit/f79f7baef441826f14b613f9749e88e2a429d191))
* add explicit give and put-away controls for presents ([ad8e7b0](https://github.com/jrepp/jelligothci/commit/ad8e7b05c71f23b7c34970e1416b670999795275))
* add persistent hydration and water care ([a12c374](https://github.com/jrepp/jelligothci/commit/a12c374999ca25efd9915921eba3df30a39e7a73))
* add pixel-art barbell and water icons ([4524f88](https://github.com/jrepp/jelligothci/commit/4524f88715b9914c83b84603886a3c92600aacfb))
* add Wi-Fi time sync and OTA foundation ([b5f1997](https://github.com/jrepp/jelligothci/commit/b5f19975385b2aefafe97d99f8e31b34a9db58ec))
* browse pets and evolutions through a nine-slot collection ([e8591dc](https://github.com/jrepp/jelligothci/commit/e8591dc2ddf94f0e5e9ad873a7c0900a0c9e7c9d))
* **jelli-art:** add the Jelli Art pixel studio, container image, and art polish pass ([#5](https://github.com/jrepp/jelligothci/issues/5)) ([f2912da](https://github.com/jrepp/jelligothci/commit/f2912da7f12ab7e003f0e3c11c45783d6451aca7))


### Bug Fixes

* restore green CI for collection, exercise, and debug host checks ([#6](https://github.com/jrepp/jelligothci/issues/6)) ([2369614](https://github.com/jrepp/jelligothci/commit/2369614a509dc36bd229186ae5b25f4e95a01aa6))

## [0.2.0](https://github.com/jrepp/jelligothci/compare/v0.1.1...v0.2.0) (2026-10-08)


### Features

* add bounded input color animations and faster rendering ([e230f5f](https://github.com/jrepp/jelligothci/commit/e230f5fd036f3c94c30e5b91a440e5be25e900c1))
* add collectible presents, linked sleep, and live artwork ([153a6a7](https://github.com/jrepp/jelligothci/commit/153a6a71176fb9af4a8b9f54863cda1f09c1a58e))
* add expressive pet care, debug console, and native release packages ([a112e55](https://github.com/jrepp/jelligothci/commit/a112e550c33d17f297310216a49589d693a6071c))
* add playable virtual pet MVP ([efdf404](https://github.com/jrepp/jelligothci/commit/efdf404f40809cc64b22df8b1d76790dd2530c13))
* add swipe navigation and clearer pet activity controls ([b1676ef](https://github.com/jrepp/jelligothci/commit/b1676efa564b870f39ff42df1a765aa255cfd49e))
* add virtual pet slice artwork and architecture draft ([fef3ad0](https://github.com/jrepp/jelligothci/commit/fef3ad04bc01ee594d68a061a38dc0a4ff99df86))


### Bug Fixes

* avoid integer promotion in reward interpolation ([44a6163](https://github.com/jrepp/jelligothci/commit/44a616323b7972df9085f256e73a7ca602f78b32))
* improve sleep particles and simplify the sun icon ([8f44634](https://github.com/jrepp/jelligothci/commit/8f446340c58d953e14afa37b7287a00fead83445))
* keep renderer regression tests portable across compilers ([6f633d8](https://github.com/jrepp/jelligothci/commit/6f633d835d69eab130ab5852b4597a6d1c2c22fe))
* keep screenshot encoding warning-clean with GCC ([f443b57](https://github.com/jrepp/jelligothci/commit/f443b57ddc7a073036235642c2d520a658134218))

## [0.1.1](https://github.com/jrepp/jelligothci/compare/v0.1.0...v0.1.1) (2026-10-07)


### Bug Fixes

* **build:** refresh version metadata in incremental CMake builds ([8a0c5cc](https://github.com/jrepp/jelligothci/commit/8a0c5cc394b1ed698e235d76ad1dc1128092b66b))

## 0.1.0 (2026-10-07)


### Features

* **release:** adopt semantic versions and validated source releases ([a7a3b81](https://github.com/jrepp/jelligothci/commit/a7a3b819d1ebf4420672b431b193a456d9e8bfde))


### Bug Fixes

* **ci:** bound dependency download retries and timeouts ([c1d1015](https://github.com/jrepp/jelligothci/commit/c1d1015296c3df8f620613b4f4d05985ec83eb4b))
* **release:** start the MVP at version 0.1.0 ([4065e8c](https://github.com/jrepp/jelligothci/commit/4065e8cf8482db68f02fedcafbf4095d25f912c7))
* **tooling:** keep uv local under runner image environment overrides ([86fc319](https://github.com/jrepp/jelligothci/commit/86fc31916961a9002526507b8b9686d3df882f17))

## Changelog

Release Please maintains this file from Conventional Commits.
