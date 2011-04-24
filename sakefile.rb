# -*- ruby -*-
# sake variant sake3

#
# sakefile.rb
#
# Copyright 2008 Helsinki Institute for Information Technology (HIIT)
# and the authors. All rights reserved.
#
# Authors: Tero Hasu <tero.hasu@hut.fi>
#

# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# The build tool driving this makefile is a custom one. The required
# version has not yet been released.

require 'sake3/component'

def try_load file
  begin
    load file
  rescue LoadError; end
end

$uid_v8 = 0x0442244e
$basename = "pyaosocket"

$proj = Sake::Project.new(:basename => $basename,
                          :name => "AoSocket Library for PyS60",
                          :version => [2, 3],
                          # This is a test UID.
                          :uid => Sake::Uid.v8($uid_v8),
                          :vendor => "HIIT")

class <<$proj
  def pkg_in_file
    group_dir + ("module.pkg.in")
  end
end

$pyd = Sake::Component.new(:project => $proj,
                           :target_type => :pyd,
                           :basename => $basename,
                           :bin_basename => $basename,
                           :uid3 => Sake::Uid.v8($uid_v8),
                           :caps => Sake::ALL_CAPS)

class <<$pyd
  def mmp_in_file
    group_dir + ("module.mmp.in")
  end
end

$comp_list = [$pyd].compact

if $sake_op[:kits]
  $kits = Sake::DevKits::get_exact_set($sake_op[:kits].strip.split(/,/))
else
  $kits = Sake::DevKits::get_all
end

$kits.delete_if do |kit|
  !kit.supports_python?
end

if $sake_op[:comps]
  comps = $sake_op[:comps].strip.split(/,/)
  $comp_list.delete_if do |comp|
    !comps.include?(comp.basename)
  end
end

$builds = $kits.map do |kit|
  build = Sake::ProjBuild.new(:project => $proj,
                              :devkit => kit)
  build.abld_platform = (build.v9? ? "gcce" : "armi")
  build.abld_build = ($sake_op[:udeb] ? "udeb" : "urel")
  if $sake_op[:udeb]
    build.handle = (build.handle + "_udeb")
  end
  build
end

# For any v9 builds, configure certificate info for signing.
try_load('local/signing.rb')

$builds.delete_if do |build|
  (build.sign and !build.cert_file)
end

if $sake_op[:builds]
  blist = $sake_op[:builds]
  $builds.delete_if do |build|
    !blist.include?(build.handle)
  end
end

desc "Prints a list of possible builds."
task :builds do
  for build in $builds
    puts build.handle
  end
end

desc "Prints info about possible builds."
task :build_info do
  for build in $builds
    puts "#{build.handle}:"
    puts "  project name  #{build.project.name}"
    puts "    basename    #{build.project.basename}"
    puts "  target        #{build.target.handle}"
    puts "  devkit        #{build.devkit.handle}"
    puts "  abld platform #{build.abld_platform}"
    puts "  abld build    #{build.abld_build}"
    puts "  sign SIS?     #{build.sign}"
    puts "  cert file     #{build.cert_file}"
    puts "  privkey file  #{build.key_file}"
    puts "  cert caps     #{build.max_caps.inspect}"
    puts "  components    #{build.comp_builds.map {|x| x.component.basename}.inspect}"
  end
end

class HexNum
  def initialize num
    @num = num
  end

  def to_s
    "0x%08x" % @num
  end
end

$exeb = Hash.new
for build in $builds
  map = build.trait_map

  # To define __UID__ for header files.
  if build.uid
    map[:uid] = HexNum.new(build.uid.number)
  end

  # NDEBUG controls whether asserts are to be compiled in (NDEBUG is
  # defined in UDEB builds). Normally an assert results in something
  # being printed to the console. To also have the errors logged, you
  # will want to enable logging by setting "logging=true". Without
  # this setting, there will be no dependency on the (deprecated) file
  # logger API, and errors are still displayed on the console (if you
  # have one and have time to read it). "logging=true" has no effect
  # if your SDK does not have the required API.
  if $sake_op[:logging] and map[:has_flogger]
    map[:do_logging] = :define
  end

  # Each build variant shall have all of the components.
  build.comp_builds = $comp_list.map do |comp|
    b = Sake::CompBuild.new(:proj_build => build,
                            :component => comp)
    $exeb[build] = b
    b
  end
end

task :default => [:bin, :sis]

require 'sake3/tasks'

Sake::Tasks::def_list_devices_tasks(:builds => $builds)

Sake::Tasks::def_makefile_tasks(:builds => $builds)

Sake::Tasks::def_binary_tasks(:builds => $builds)

Sake::Tasks::def_sis_tasks(:builds => $builds)

Sake::Tasks::def_clean_tasks(:builds => $builds)

task :all => [:makefiles, :bin, :sis]

$doc_build = $builds.last

# Configure any rules related to releasing and uploading and such
# things. Probably at least involves copying or uploading the
# distribution files somewhere.
try_load('local/releasing.rb')

Sake::Tasks::force_uncurrent_on_op_change

task :web do
  srcfiles = Dir['web/*.txt2tags.txt']
  sh("darcs changes > web/changelog.txt")
  for srcfile in srcfiles
    htmlfile = srcfile.sub(/\.txt2tags\.txt$/, ".html")
    sh("tools/txt2tags --target xhtml --infile %s --outfile %s --encoding utf-8 --verbose" % [srcfile, htmlfile])
  end
end

desc "Prepares downloads for the current version."
task :release do
  web_dir = "download"

  mkdir_p web_dir

  for build in $builds
    ## Unsigned.
    if (not build.sign_sis?) or (build.sign_sis? and ($cert_name == "dev"))
      src_sis_file = build.to_proj_rel(build.long_sis_file).to_s
      sis_basename = File.basename(src_sis_file)
      download_file = File.join(web_dir, sis_basename)
      ln(src_sis_file, download_file, :force => true)
    end

    ## Signed.
    if build.sign_sis? and ($cert_name == "self")
      src_sis_file = build.to_proj_rel(build.long_sisx_file).to_s
      sis_basename = File.basename(src_sis_file)
      download_file = File.join(web_dir, sis_basename)
      ln(src_sis_file, download_file, :force => true)
    end
  end
end

def sis_info opt
  for build in $builds
    if build.short_sisx_file.exist?
      sh("sisinfo -f #{build.short_sisx_file} #{opt}")
    end
  end
end

task :sis_ls do
  sis_info "-i"
end

task :sis_cert do
  sis_info "-c"
end

task :sis_struct do
  sis_info "-s"
end
