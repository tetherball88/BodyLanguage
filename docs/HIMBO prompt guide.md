# How to Use the HIMBO Description Prompt

This guide explains how to generate consistent **clothed/naked body descriptions** from HIMBO BodySlide presets using the prompt.

---

## ✅ Preferred/Tested Model
- **Claude Sonnet 3.7 (thinking mode)**  
  This model has been tested and works best for parsing, calculating, and producing reliable results.

---

## 📝 Step-by-Step Usage

1. **Paste the prompt**  
   Copy the full [HIMBO Description Prompt](./HIMBO_prompt.md) into the LLM text input.

2. **Upload or paste XML**  
   - Preferably upload a BodySlide preset `.xml` file that contains a **single preset**.  
   - Alternatively, paste the XML code immediately after the prompt.

3. **Wait for processing**  
   Allow the LLM to parse the sliders, calculate trait aggregates, and output the JSON body descriptions.

4. **Verify the preset name**  
   - Check that the `name` in the XML **exactly matches** the key in the generated JSON.  
   - ⚠️ If names do not match, the descriptions won’t connect properly in-game.

5. **Review the generated descriptions**  
   - Compare descriptions against how the body actually looks in BodySlide.  
   - Use the LLM’s *thinking process* to confirm it read all relevant sliders (and didn’t miss or misinterpret values).

6. **Request tweaks if needed**  
   - You can ask the LLM to adjust details — e.g., make shoulders wider, more chubby/lean, etc. — and regenerate the JSON.

7. **One preset per chat**  
   - It’s best to process only **one preset per conversation**.  
   - If you want to work on another preset, start a **new chat session**.

---

## 🎚️ Weight Distribution in Game

- The prompt generates descriptions for three anchor weights:  
  - **0** → description for very low body weight  
  - **100** → description for maximum body weight  
  - **50** → interpolated midpoint between 0 and 100  

- Skyrim distributes these ranges as:  
  - **0 description** → applied to in-game weights 0–33  
  - **50 description** → applied to in-game weights 34–66  
  - **100 description** → applied to in-game weights 67–100  

This ensures smooth variation across character body morphs.

---

## ⚠️ Notes on Removed/Unsupported Sliders
- Some presets may still contain **deprecated or removed sliders** (e.g., *SamuelLike, SOSlike, etc*).  
- This prompt is designed to **ignore any unsupported sliders**.  
- Skyrim will also ignore these sliders in-game, meaning:  
  - The generated descriptions represent the **actual in-game result**,  
  - They may differ from screenshots shown on mod pages (which might use old presets).

---

## 🚀 Updating the Repository

When your JSON is ready:

1. Copy the generated JSON block.
2. Paste it at the **bottom** of:
```
SKSE\Plugins\BodyLanguage\himboDescriptions.json
```

3. To share your update:
- Create a Pull Request (PR), **or**  
- Ping me on Discord and I’ll add it to the repository manually.

---