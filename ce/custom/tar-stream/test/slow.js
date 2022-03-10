var test = require('tape')
var stream = require('stream')
var zlib = require('zlib')
var fs = require('fs')
var tar = require('../')
var fixtures = require('./fixtures')

test('huge', function (t) {
  t.plan(1)

  var extract = tar.extract()
  var noEntries = false
  var hugeFileSize = 8804630528 // ~8.2GB
  var dataLength = 0

  var countStream = new stream.Writable()
  countStream._write = function (chunk, encoding, done) {
    dataLength += chunk.length
    done()
  }

  // Make sure we read the correct pax size entry for a file larger than 8GB.
  extract.on('entry', function (header, stream, callback) {
    t.deepEqual(header, {
      devmajor: 0,
      devminor: 0,
      gid: 20,
      gname: 'staff',
      linkname: null,
      mode: 420,
      mtime: new Date(1521214967000),
      name: 'huge.txt',
      pax: {
        'LIBARCHIVE.creationtime': '1521214954',
        'SCHILY.dev': '16777218',
        'SCHILY.ino': '91584182',
        'SCHILY.nlink': '1',
        atime: '1521214969',
        ctime: '1521214967',
        size: hugeFileSize.toString()
      },
      size: hugeFileSize,
      type: 'file',
      uid: 502,
      uname: 'apd4n'
    })

    noEntries = true
    stream.pipe(countStream)
  })

  extract.on('finish', function () {
    t.ok(noEntries)
    t.equal(dataLength, hugeFileSize)
  })

  var gunzip = zlib.createGunzip()
  var reader = fs.createReadStream(fixtures.HUGE)
  reader.pipe(gunzip).pipe(extract)
})
