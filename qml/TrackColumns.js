// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

.pragma library

var ALL_COLUMNS = [
    {
        key: "favorite",
        defaultWidth: 36,
        sortKey: -1,
        defaultVisible: true,
        alignRight: false,
        canHide: true
    },
    {
        key: "trackNumber",
        defaultWidth: 48,
        sortKey: -1,
        defaultVisible: false,
        alignRight: true,
        canHide: true
    },
    {
        key: "title",
        defaultWidth: 200,
        sortKey: 1, // TrackListModel.Title
        defaultVisible: true,
        alignRight: false,
        canHide: false
    },
    {
        key: "artist",
        defaultWidth: 180,
        sortKey: 2, // TrackListModel.Artist
        defaultVisible: true,
        alignRight: false,
        canHide: true
    },
    {
        key: "album",
        defaultWidth: 200,
        sortKey: 3, // TrackListModel.Album
        defaultVisible: true,
        alignRight: false,
        canHide: true
    },
    {
        key: "albumArtist",
        defaultWidth: 160,
        sortKey: -1,
        defaultVisible: false,
        alignRight: false,
        canHide: true
    },
    {
        key: "genre",
        defaultWidth: 120,
        sortKey: -1,
        defaultVisible: false,
        alignRight: false,
        canHide: true
    },
    {
        key: "year",
        defaultWidth: 64,
        sortKey: 4, // TrackListModel.Year
        defaultVisible: true,
        alignRight: true,
        canHide: true
    },
    {
        key: "duration",
        defaultWidth: 72,
        sortKey: 5, // TrackListModel.Duration
        defaultVisible: true,
        alignRight: true,
        canHide: true
    },
    {
        key: "rating",
        defaultWidth: 96,
        sortKey: -1,
        defaultVisible: true,
        alignRight: false,
        canHide: true
    },
    {
        key: "format",
        defaultWidth: 110,
        sortKey: -1,
        defaultVisible: false,
        alignRight: false,
        canHide: true
    },
    {
        key: "bitrate",
        defaultWidth: 90,
        sortKey: -1,
        defaultVisible: false,
        alignRight: true,
        canHide: true
    },
    {
        key: "addedAt",
        defaultWidth: 110,
        sortKey: 6, // TrackListModel.DateAdded
        defaultVisible: false,
        alignRight: false,
        canHide: true
    }
];

function defaultVisibleKeys() {
    var keys = [];
    for (var i = 0; i < ALL_COLUMNS.length; i++) {
        if (ALL_COLUMNS[i].defaultVisible) {
            keys.push(ALL_COLUMNS[i].key);
        }
    }
    return keys;
}

function defaultColumnWidths() {
    var widths = {};
    for (var i = 0; i < ALL_COLUMNS.length; i++) {
        widths[ALL_COLUMNS[i].key] = ALL_COLUMNS[i].defaultWidth;
    }
    return widths;
}

function getColumnByKey(key) {
    for (var i = 0; i < ALL_COLUMNS.length; i++) {
        if (ALL_COLUMNS[i].key === key) {
            return ALL_COLUMNS[i];
        }
    }
    return null;
}

function formatCodec(codec, sampleRate, bitDepth) {
    if (!codec) {
        return "";
    }
    var upper = codec.toUpperCase();
    var suffix = "";
    if (sampleRate > 0 || bitDepth > 0) {
        var srText = "";
        if (sampleRate > 0) {
            var khz = sampleRate / 1000.0;
            srText = (khz % 1 === 0) ? khz.toFixed(0) : khz.toFixed(1);
        }
        if (bitDepth > 0 && srText.length > 0) {
            suffix = " " + bitDepth + "/" + srText;
        } else if (bitDepth > 0) {
            suffix = " " + bitDepth + "bit";
        } else if (srText.length > 0) {
            suffix = " " + srText + "kHz";
        }
    }
    return upper + suffix;
}

function formatAddedAt(addedAt) {
    if (!addedAt || addedAt <= 0) {
        return "";
    }
    var d = new Date(addedAt);
    if (isNaN(d.getTime())) {
        return "";
    }
    var year = d.getFullYear();
    var month = String(d.getMonth() + 1).padStart(2, "0");
    var day = String(d.getDate()).padStart(2, "0");
    return year + "-" + month + "-" + day;
}
