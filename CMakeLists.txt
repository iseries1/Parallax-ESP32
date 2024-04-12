idf_component_register(SRCS "Parallax-ESP32.c" "config.c" "serbridge.c" "discovery.c" "httpd.c" "settings.c" "json.c" "captdns.c" "status.c" "parser.c" "cmds.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "upload_script.html")

# Create a SPIFFS image from the contents of the 'spiffs_image' directory
# that fits the partition named 'storage'. FLASH_IN_PROJECT indicates that
# the generated image should be flashed when the entire project is flashed to
# the target with 'idf.py -p PORT flash'.
spiffs_create_partition_image(storage ../html FLASH_IN_PROJECT)
