# TT Body Language 🎭

✨ Welcome to the magical world of enhanced NPC descriptions! This Skyrim mod brings your characters to life by providing rich, detailed body shape information to SkyrimNet. Whether you're using BHUNP, CBBE 3BA, or HIMBO body types, I've got you covered! And for those spicy playthroughs, I even support TNG (The New Gentleman) integration! 🔥

> 💡 **Important Note**: The mod comes with pre-generated descriptions for popular body presets, but the real magic happens when the community contributes! Feel free to generate and share descriptions for your favorite presets using our guides below.

## Requirements 📋

- [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin)
- [OBody NG](https://www.nexusmods.com/skyrimspecialedition/mods/77016) (I used version 4.3.7)
- [The New Gentleman (Optional)](https://www.nexusmods.com/skyrimspecialedition/mods/104215) (I used version 4.2.3)

## Features ✨

- 📚 Ready-to-use pre-generated descriptions for popular body presets
- 🎯 Smart context-aware body descriptions that know when NPCs are clothed or naked
- 👗 Supports your favorite body types:
  - BHUNP 💃
  - CBBE 3BA 👒
  - HIMBO 💪
- 🎭 Three flavors of descriptions:
  - Clothed (for those fully dressed moments) 👔
  - Naked (when they're living their best life) 🌟
  - Male Genital (when TNG is in play) 🔞

## Integration with SkyrimNet 🤖

The body descriptions will appear in SkyrimNet prompts in the following scenarios:
- 📝 Whenever an NPC's bio is requested
- 💬 During dialogues for:
  - The main speaking NPC
  - The target NPC (who's being talked about)
- ⚠️ Currently does not appear for nearby NPCs (planned for future enhancement)

## Community Contributions 🤝
This mod thrives on community participation! 

### Body descriptions

[How to add more presets?](./docs/BodyDescriptionSystem.md)

### TNG descriptions
While I provide descriptions for default TNG addons, I encourage you to:
- If you use TNG addons create your json file, like:
```json
{
    "__metaInfo": {
        "typeName": "JFormMap"
    },
    "__formData|{name of addon plugin fule}|{form of armor addon}": "your addon description"
}
```
- Addon description should fit in sentence like this: `{{npcName}}'s exposed penis is {{size description}}. It has a {{tngAddon}}`
- Add more addon descriptions here: [SKSE\Plugins\SkyrimNet\jsonData\tngAddons.json](.\SKSE\Plugins\SkyrimNet\jsonData\tngAddons.json)
- Use addon's NAME as key
- Load your game and you should have this descriptions in LLM prompts
- Share your generated descriptions with others

Check out our current collection of preset descriptions:
- [BHUNP Body Presets](./docs/BHUNP_descriptions.md)
- [CBBE 3BA Body Presets](./docs/CBBE_3BA_descriptions.md)
- [HIMBO Body Presets](./docs/HIMBO_descriptions.md)

The process is simple - just follow our guides below to create descriptions that match your specific BodySlide presets!

## Documentation 📖

The `docs` folder contains detailed guides for using prompts with BodySlide preset files:
- [New preferred method for body descriptions!](.\docs\BodyDescriptionSystem.md)
- [BHUNP prompt guide](./docs/BHUNP%20prompt%20guide.md)
- [CBBE 3BA prompt guide](./docs/CBBE%203BA%20prompt%20guide.md)
- [HIMBO prompt guide](./docs/HIMBO%20prompt%20guide.md)

Each guide explains how to use the corresponding prompts to generate accurate body descriptions for different body types.

## Utilities 🛠️

### ParseBodySlideXml ⚙️

Want to create descriptions for your own body presets? Our handy Node.js utility in the `parseBodySlideXml` directory is here to help! 

✨ What it does:
- 📊 Extracts all the juicy slider data from your body types
- 🔄 Processes BodySlide preset files like a charm
- 🎯 Prepares everything perfectly for our description prompts

🚀 Ready to roll? Here's how:
1. 📂 Navigate to the `parseBodySlideXml` directory
2. 📦 Install the goodies:
   ```
   npm install
   ```
3. ✨ Let the magic happen:
   ```
   node build-sliders.js
   ```


Once you've generated your descriptions, consider sharing them with the community! Together, we can create a rich library of body descriptions for everyone to enjoy! 🌟
