# 宁州地图插画

2026-10-11 使用内置 image_gen 工具生成并复制到本项目，均为不透明 PNG。生成原图保留在 Codex generated_images 中；游戏离线加载这里的素材。

当前地图的28处地点使用 [locations/ 中的独立插画](locations/README.md)。新版舆图按用户三张截图重画，西部沙地、北部火山、东北金虹与东部海岸对应相对方位，实际叠加坐标见 [location-reference.json](location-reference.json)。最新版底图的提示词与生成源路径等元数据仅保留在开发者本地，未纳入公开仓库；游戏运行只需本目录中的成品 PNG。以下底图提示词和通用风景为旧版设计记录。

- ningzhou-atlas.webp：水墨舆图底图，地图坐标和路线由前端叠加。
- city-scene.webp：城池景观。
- sect-scene.webp：宗门山门景观。
- wild-scene.webp：险地山川景观。

下列为实际生成描述，未使用 CLI 或外部图片下载。

## ningzhou-atlas.webp

```text
Use case: game-asset. Create a finished landscape 16:10 illustration for the background of an interactive Chinese cultivation fantasy world map called Ningzhou. A quiet top-down and gently oblique hand-painted ink-and-watercolor atlas on pale warm silk paper. Muted jade, moss green, indigo water and antique gold accents, refined traditional Chinese landscape brushwork, polished strategy RPG environment art. The land occupies the central and left 88 percent, the southeastern sea occupies the right edge especially the bottom-right; meandering rivers connect small valleys, mountain ridges toward the north and western corners. Sparse miniature ancient Chinese town rooftops and mountain temple complexes, bamboo forest on the west, warm rock mountains on the east, mineral hills in southwest, much open uncluttered silk area for interactive location labels. The playable overlays will put cities around (51%,58%),(24%,36%),(73%,35%),(78%,69%),(27%,73%); monasteries around (18%,20%),(83%,18%),(14%,57%),(89%,52%),(38%,87%); a mountain at (48%,24%) and open sea at (94%,83%). These coordinates are composition hints only; do not draw any coordinate markings. Strong fine texture and elegant scenery without competing with labels. No written text, calligraphy, place names, labels, pins, UI, borders, compass letters, characters or watermarks. Full bleed opaque image, edge-to-edge scenery.
```

## city-scene.webp

```text
Use case: game-asset. Create a finished landscape 16:9 location portrait illustration for an ancient Chinese cultivation fantasy game, a prosperous Ningzhou riverside city. Traditional elegant ink-and-watercolor painting on pale warm silk, muted jade and moss greens with antique gold dusk lights, detailed but calm hand-painted RPG environment art. Layered curved tiled roofs, a stone bridge over a river, an inviting market street and distant misty mountains, small lantern glow, airy depth. A low near-human perspective overlooking the city from a river terrace. Scenic architecture is the subject, no named character. Beautiful readable focal architecture in center-left, soft atmospheric edges, full-bleed opaque art. No words, lettering, typography, logos, captions, UI, borders or watermark.
```

## sect-scene.webp

```text
Use case: game-asset. Create a finished landscape 16:9 location portrait illustration for an ancient Chinese cultivation fantasy game, a secluded mountain cultivation sect. Traditional refined ink-and-watercolor painting on pale warm silk, muted jade and indigo with antique gold details, cohesive hand-painted RPG environment art. Monumental graceful Chinese temple halls above a sea of clouds, weathered ceremonial stairs, pine trees, layered swordlike mountain peaks, subtle suspended mist suggesting spiritual energy. A low angled view toward a mountain gate and higher temple roofs, grand serene ancient atmosphere. Central architectural focal point fully visible for use in an interactive destination detail card. Full bleed opaque art. No written Chinese characters on gates, no calligraphy, any text, logos, captions, UI, borders or watermarks.
```

## wild-scene.webp

```text
Use case: game-asset. Create a finished landscape 16:9 location portrait illustration for an ancient Chinese cultivation fantasy game, mysterious wilderness of Ningzhou. Refined traditional ink-and-watercolor painting on pale warm silk, muted jade, moss green, stone ochre, indigo water and antique gold small accents, cohesive detailed hand-painted RPG environment art. Tall misty mountain crags above a winding turquoise stream flowing out toward a distant southeastern sea, ancient pines and bamboo, small clusters of luminous medicinal herbs, an old stone path disappearing into mist. Untamed beautiful landscape suggesting real travel and adventure, calm but with deep distant mystery. Center focal ridge and water visible, full bleed opaque art. No people as focal subjects, no written text, calligraphy, place names, labels, UI, borders or watermarks.
```

