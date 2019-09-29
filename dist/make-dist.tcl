if {[llength [info commands console]]} {
    console show
    update
}

set QT_DIR C:/Programming/QtSDK2015/5.9.3/msvc2017_64
set BIN C:/Programming/TileZed/dist64
set SRC C:/Programming/TileZed/tiled
set DEST {C:\Users\Tim\Desktop\ProjectZomboid\Tools\TileZed}
set SUFFIX "-64bit"
set SUFFIX2 ""
set REDIST vcredist.x64.exe

if {$argc > 0} {
    switch -- [lindex $argv 0] {
        32bit {
            puts "dist.tcl: 32-bit"
            append DEST "32"
            set BIN C:/Programming/TileZed/dist32
            set QT_DIR C:/Programming/QtSDK2015/5.9.3/msvc2017
            set SUFFIX "-32bit"
            set SUFFIX2 "32"
            set REDIST vcredist.x86.exe
        }
        64bit {
            puts "dist.tcl: 64-bit"
        }
        default {
            error "unknown arguments to dist.tcl: $argv"
        }
    }
}

set QT_BINARY_DIR $QT_DIR/bin
set QT_PLUGINS_DIR $QT_DIR/plugins
set QT_TRANSLATIONS_DIR $QT_DIR/translations

proc copyFile {SOURCE DEST name {name2 ""}} {
    if {$name2 == ""} { set name2 $name }
    set src [file join $SOURCE $name]
    set dst [file join $DEST $name2]
    if {![file exists $src]} {
        error "no such file \"$src\""
    }
    set relative $name
    foreach var {BIN SRC QT_BINARY_DIR QT_PLUGINS_DIR QT_TRANSLATIONS_DIR} {
        if {[string match [set ::$var]* $src]} {
            set relative [string range $src [string length [set ::$var]] end]
        }
    }
    if {![file exists $dst] || ([file mtime $src] > [file mtime $dst]) || ([file size $src] != [file size $dst])} {
        file mkdir [file dirname $dst]
        if {[file extension $name2] == ".txt"} {
            set chan [open $src r]
            set text [read $chan]
            close $chan
            set chan [open $dst w]
            fconfigure $chan -translation crlf
            puts -nonewline $chan $text
            close $chan
            puts "copied $relative (crlf)"
        } else {
            file copy -force $src $dst
            puts "copied $relative"
        }
    } else {
        puts "skipped $relative"
    }
    return
}

proc copyDir {SOURCE DEST name {name2 ""}} {
    if {$name2 == ""} { set name2 $name }
    set src [file join $SOURCE $name]
    set dst [file join $DEST $name2]
    if {![file exists $src]} {
        error "no such directory \"$src\""
    }
    foreach f [glob -nocomplain -tails -dir $src *] {
        if {$f == "." || $f == ".."} continue
        if {[file isdirectory $src/$f]} {
            copyDir $src $dst $f
        } else {
            copyFile $src $dst $f
        }
    }
}

proc createFile {DEST name contents} {
    set chan [open $DEST/$name w]
    fconfigure $chan -translation crlf
    puts -nonewline $chan $contents
    close $chan
    puts "created $DEST/$name"
}

puts ---Toplevel---
copyFile {C:\Programming\TileZed} $DEST $REDIST
copyFile $BIN $DEST config.exe
copyFile $BIN $DEST TileZed.exe
copyFile $BIN $DEST tiled.dll
copyFile $BIN $DEST tmxviewer.exe
copyFile $BIN $DEST zlib1.dll

copyFile $SRC $DEST AUTHORS AUTHORS.txt
copyFile $SRC $DEST COPYING COPYING.txt
copyFile $SRC $DEST LICENSE.BSD LICENSE.BSD.txt
copyFile $SRC $DEST LICENSE.GPL LICENSE.GPL.txt
copyFile $SRC $DEST LICENSE.QT5 LICENSE.QT5.txt
copyFile $SRC $DEST NEWS NEWS.txt
copyFile $SRC $DEST README.md README.txt

createFile $DEST qt.conf {[Paths]
Plugins = plugins
Translations = translations
}

copyFile $SRC $DEST LuaTools.txt
copyFile $SRC $DEST Rearrange.txt
copyFile $SRC $DEST TileProperties.txt
copyFile $SRC $DEST Tilesets.txt

puts ---Translations---
set qt_trs {qt_cs.qm qt_de.qm qt_es.qm qt_fr.qm qt_he.qm qt_ja.qm qt_pt.qm qt_ru.qm qt_zh_CN.qm qt_zh_TW.qm}
foreach name $qt_trs {
    copyFile $QT_TRANSLATIONS_DIR $DEST/translations $name
}

set TRS {
    tiled_ru.qm
    tiled_en.qm
    tiled_lv.qm
    tiled_it.qm
    tiled_zh.qm
    tiled_fr.qm
    tiled_cs.qm
    tiled_pt.qm
    tiled_nl.qm
    tiled_pt_BR.qm
    tiled_he.qm
    tiled_es.qm
    tiled_de.qm
    tiled_ja.qm
}
foreach name $TRS {
    copyFile $BIN/translations $DEST/translations $name
}

puts ---BuildingEd---
foreach name {BuildingFurniture.txt BuildingTemplates.txt BuildingTiles.txt TMXConfig.txt} {
    copyFile $SRC/src/tiled/BuildingEditor $DEST $name
}
copyDir $SRC/src/tiled/BuildingEditor $DEST/docs manual BuildingEd

puts ---Docs---
copyFile $SRC/docs $DEST/docs map.dtd
copyFile $SRC/docs $DEST/docs map.xsd
copyDir $SRC/docs $DEST/docs TileProperties
copyDir $SRC/docs $DEST/docs TileZed

puts ---Examples---
copyDir $SRC $DEST examples

puts ---Plugins---
foreach plugin {droidcraft flare json lot lua replicaisland tengine tmw} {
    copyFile $BIN/plugins/tiled $DEST/plugins/tiled $plugin.dll
}

puts ---Lua---
copyDir $SRC $DEST lua

puts "---Qt DLLS---"
copyFile $QT_BINARY_DIR $DEST Qt5Core.dll
copyFile $QT_BINARY_DIR $DEST Qt5Gui.dll
copyFile $QT_BINARY_DIR $DEST Qt5Network.dll
copyFile $QT_BINARY_DIR $DEST Qt5OpenGL.dll
copyFile $QT_BINARY_DIR $DEST Qt5Widgets.dll
copyFile $QT_BINARY_DIR $DEST Qt5Xml.dll
if {[file exists $QT_BINARY_DIR/icudt54.dll]} {
copyFile $QT_BINARY_DIR $DEST icudt54.dll
copyFile $QT_BINARY_DIR $DEST icuin54.dll
copyFile $QT_BINARY_DIR $DEST icuuc54.dll
}

copyFile $QT_PLUGINS_DIR $DEST/plugins imageformats/qgif.dll
copyFile $QT_PLUGINS_DIR $DEST/plugins imageformats/qjpeg.dll
copyFile $QT_PLUGINS_DIR $DEST/plugins imageformats/qtiff.dll

copyFile $QT_PLUGINS_DIR $DEST/plugins platforms/qwindows.dll

#copyFile $QT_PLUGINS_DIR $DEST/plugins codecs/qcncodecs4.dll
#copyFile $QT_PLUGINS_DIR $DEST/plugins codecs/qjpcodecs4.dll
#copyFile $QT_PLUGINS_DIR $DEST/plugins codecs/qkrcodecs4.dll
#copyFile $QT_PLUGINS_DIR $DEST/plugins codecs/qtwcodecs4.dll

if false {
### Archive creation
puts "---Archive Creation---"
set date [clock format [clock seconds] -format "%b-%d-%Y"]
set name TileZed-$date$SUFFIX.zip
set ARCHIVE C:/Users/Tim/Desktop/ProjectZomboid/$name
file delete $ARCHIVE
cd C:/Users/Tim/Desktop/ProjectZomboid/Tools
exec {C:\Program Files\7-Zip\7z.exe} a $ARCHIVE TileZed$SUFFIX2
cd C:/Programming/TileZed
puts $name
}
