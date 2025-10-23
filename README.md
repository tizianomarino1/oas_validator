# oas_validator

Validatore OpenAPI 3.0

## Compilazione

### Windows (MSVC)
1. Apri il **Developer Command Prompt for Visual Studio** (la shell con gli strumenti di compilazione `cl.exe`).
2. Spostati nella cartella del progetto:
   ```cmd
   cd \percorso\alla\cartella\oas_validator
   ```
3. Crea la cartella di output e compila indicando la destinazione dei binari in `build\`:
   ```cmd
   if not exist build mkdir build
   cl /W4 /O2 /std:c11 /Iinclude /Iexternal src\main.c src\fileutil.c src\oas_extract.c src\jsonschema.c external\cJSON.c external\miniyaml.c /Fe:build\oas_validator.exe
   ```

### Linux (GCC o Clang)
1. Assicurati di avere installato un compilatore C11 (ad esempio `gcc` o `clang`) e gli strumenti di base per la build (`build-essential` su Debian/Ubuntu).
2. Dalla cartella del progetto esegui:
   ```bash
   mkdir -p build
   gcc -std=c11 -Wall -Wextra -O2 -Iinclude -Iexternal src/*.c external/cJSON.c external/miniyaml.c -o build/oas_validator
   ```
   (Sostituisci `gcc` con `clang` se preferisci.)

L'eseguibile risultante (in `build/oas_validator.exe` su Windows oppure `build/oas_validator` su Linux) accetta due argomenti:

```bash
./build/oas_validator richiesta.json openapi.yaml
```

Entrambi i file di input possono essere in formato JSON o YAML: il programma riconosce automaticamente il formato da validare.
Il programma stampa `VALIDO ✅` quando il payload fornito rispetta lo schema individuato nella specifica OpenAPI 3.x, altrimenti indica l'errore.
