var test = require('tape')
var tar = require('../index')
var fixtures = require('./fixtures')
var concat = require('./lib/concat-stream')
var fs = require('fs')
var Writable = require('stream').Writable

test('one-file', function (t) {
  t.plan(2)

  var pack = tar.pack()

  pack.entry({
    name: 'test.txt',
    mtime: new Date(1387580181000),
    mode: parseInt('644', 8),
    uname: 'maf',
    gname: 'staff',
    uid: 501,
    gid: 20
  }, 'hello world\n')

  pack.finalize()

  pack.pipe(concat(function (data) {
    t.same(data.length & 511, 0)
    t.deepEqual(data, fs.readFileSync(fixtures.ONE_FILE_TAR))
  }))
})

test('multi-file', function (t) {
  t.plan(2)

  var pack = tar.pack()

  pack.entry({
    name: 'file-1.txt',
    mtime: new Date(1387580181000),
    mode: parseInt('644', 8),
    uname: 'maf',
    gname: 'staff',
    uid: 501,
    gid: 20
  }, 'i am file-1\n')

  pack.entry({
    name: 'file-2.txt',
    mtime: new Date(1387580181000),
    mode: parseInt('644', 8),
    size: 12,
    uname: 'maf',
    gname: 'staff',
    uid: 501,
    gid: 20
  }).end('i am file-2\n')

  pack.finalize()

  pack.pipe(concat(function (data) {
    t.same(data.length & 511, 0)
    t.deepEqual(data, fs.readFileSync(fixtures.MULTI_FILE_TAR))
  }))
})

test('pax', function (t) {
  t.plan(2)

  var pack = tar.pack()

  pack.entry({
    name: 'pax.txt',
    mtime: new Date(1387580181000),
    mode: parseInt('644', 8),
    uname: 'maf',
    gname: 'staff',
    uid: 501,
    gid: 20,
    pax: { special: 'sauce' }
  }, 'hello world\n')

  pack.finalize()

  pack.pipe(concat(function (data) {
    // fs.writeFileSync('tmp.tar', data)
    t.same(data.length & 511, 0)
    t.deepEqual(data, fs.readFileSync(fixtures.PAX_TAR))
  }))
})

test('types', function (t) {
  t.plan(2)
  var pack = tar.pack()

  pack.entry({
    name: 'directory',
    mtime: new Date(1387580181000),
    type: 'directory',
    mode: parseInt('755', 8),
    uname: 'maf',
    gname: 'staff',
    uid: 501,
    gid: 20
  })

  pack.entry({
    name: 'directory-link',
    mtime: new Date(1387580181000),
    type: 'symlink',
    linkname: 'directory',
    mode: parseInt('755', 8),
    uname: 'maf',
    gname: 'staff',
    uid: 501,
    gid: 20,
    size: 9 // Should convert to zero
  })

  pack.finalize()

  pack.pipe(concat(function (data) {
    t.equal(data.length & 511, 0)
    t.deepEqual(data, fs.readFileSync(fixtures.TYPES_TAR))
  }))
})

test('long-name', function (t) {
  t.plan(2)
  var pack = tar.pack()

  pack.entry({
    name: 'my/file/is/longer/than/100/characters/and/should/use/the/prefix/header/foobarbaz/foobarbaz/foobarbaz/foobarbaz/foobarbaz/foobarbaz/filename.txt',
    mtime: new Date(1387580181000),
    type: 'file',
    mode: parseInt('644', 8),
    uname: 'maf',
    gname: 'staff',
    uid: 501,
    gid: 20
  }, 'hello long name\n')

  pack.finalize()

  pack.pipe(concat(function (data) {
    t.equal(data.length & 511, 0)
    t.deepEqual(data, fs.readFileSync(fixtures.LONG_NAME_TAR))
  }))
})

test('large-uid-gid', function (t) {
  t.plan(2)
  var pack = tar.pack()

  pack.entry({
    name: 'test.txt',
    mtime: new Date(1387580181000),
    mode: parseInt('644', 8),
    uname: 'maf',
    gname: 'staff',
    uid: 1000000001,
    gid: 1000000002
  }, 'hello world\n')

  pack.finalize()

  pack.pipe(concat(function (data) {
    t.same(data.length & 511, 0)
    t.deepEqual(data, fs.readFileSync(fixtures.LARGE_UID_GID))
    fs.writeFileSync('/tmp/foo', data)
  }))
})

test('unicode', function (t) {
  t.plan(2)
  var pack = tar.pack()

  pack.entry({
    name: 'høstål.txt',
    mtime: new Date(1387580181000),
    type: 'file',
    mode: parseInt('644', 8),
    uname: 'maf',
    gname: 'staff',
    uid: 501,
    gid: 20
  }, 'høllø\n')

  pack.finalize()

  pack.pipe(concat(function (data) {
    t.equal(data.length & 511, 0)
    t.deepEqual(data, fs.readFileSync(fixtures.UNICODE_TAR))
  }))
})

test('backpressure', function (t) {
  var slowWritable = new Writable({ highWaterMark: 1 })
  slowWritable._write = (chunk, enc, next) => {
    setImmediate(next)
  }

  var pack = tar.pack()
  var later = false

  setImmediate(() => {
    later = true
  })

  pack.pipe(slowWritable)

  slowWritable.on('finish', () => t.end())
  pack.on('end', () => t.ok(later))

  var i = 0
  var next = () => {
    if (++i < 25) {
      var header = {
        name: `file${i}.txt`,
        mtime: new Date(1387580181000),
        mode: parseInt('644', 8),
        uname: 'maf',
        gname: 'staff',
        uid: 501,
        gid: 20
      }

      var buffer = Buffer.alloc(1024)

      pack.entry(header, buffer, next)
    } else {
      pack.finalize()
    }
  }

  next()
})
