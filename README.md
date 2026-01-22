MQA Identifier
-
Small tool to identify MQA encoding in *.flac* files.


**Download latest release** [***here***](https://github.com/najchris11/MQA_identifier/releases)

**Gui tool availlable on releases**

# 
**Usage**

```./MQA_identifier [-v] [--dry-run] {name_of_file.flac} or {name_of_folder_to_scan} ...```

**Note:** By default, the tool will automatically add `MQAENCODER=MQA` and `ORIGINALSAMPLERATE` tags to identified MQA files if they are missing. Use `--dry-run` to disable this behavior (read-only scan).

For example
```./MQA_identifier -v "C:\Music\Mike Oldfield - Tubular Bells\(01) [Mike Oldfield] Part One.flac" "C:\Music\Queen - News Of The World"```

<br>


**More detailed instructions here** [***here***](instructions.md)

<br>
<br>

*This project isn't related nor endorsed with MQA Ltd. and is made for purely educational purposes)*

<br><br>
>  Stavros Avramidis Never Settle & Keep Running
