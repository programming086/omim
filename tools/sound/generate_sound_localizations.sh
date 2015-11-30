#!/bin/bash

# When text resources for voice notification are updated this script shall be launched
# to regenerate sound_strings.zip. Then sound_strings.zip will be used by the application for TTS systems.

set -e -u -x

MY_PATH="`dirname \"$0\"`"              # relative
MY_PATH="`( cd \"$MY_PATH\" && pwd )`"  # absolutized and normalized

readonly SOUND_TMP_DIR=$MY_PATH/../../../sound-tmp
readonly SOUND_DATA_DIR=$MY_PATH/../../sound/tts

rm -rf $SOUND_TMP_DIR
mkdir $SOUND_TMP_DIR

python $MY_PATH/sound_csv_to_sound_txt.py $SOUND_DATA_DIR/sound.csv $SOUND_DATA_DIR/sound.txt $SOUND_DATA_DIR/languages.txt

mkdir $SOUND_TMP_DIR/json
python $MY_PATH/languages.py $SOUND_DATA_DIR/languages.txt $SOUND_DATA_DIR/languages.hpp $SOUND_TMP_DIR/json/
$MY_PATH/../twine/twine --format jquery generate-all-string-files $SOUND_DATA_DIR/sound.txt $SOUND_TMP_DIR/json

readonly SOUND_OUTPUT_DIR=$MY_PATH/../../data/sound-strings/
if [ ! -d $SOUND_OUTPUT_DIR ]; then
  mkdir $SOUND_OUTPUT_DIR
fi
cp -R $SOUND_TMP_DIR/json/ $SOUND_OUTPUT_DIR
