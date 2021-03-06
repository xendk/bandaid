
Bandaid
=======

Drush tool for helping with patch management on Drupal, which helps
with patching, and upgrading modules.

See [this blog post](http://xen.dk/en/2014/04/28/point-bandaid) for the rationale behind this Drush command.

> "It's awesome" - Satisfied user

[![Build Status](https://travis-ci.org/xendk/bandaid.svg?branch=master)](https://travis-ci.org/xendk/bandaid)
[![Scrutinizer Code Quality](https://scrutinizer-ci.com/g/xendk/bandaid/badges/quality-score.png?s=baaa588ceaaa77851eba8531f75ffe1ff188b5a7)](https://scrutinizer-ci.com/g/xendk/bandaid/)
[![Code Coverage](https://scrutinizer-ci.com/g/xendk/bandaid/badges/coverage.png?s=2bff1c11061ce10cb357fedbdd684465d40e959e)](https://scrutinizer-ci.com/g/xendk/bandaid/)

Before you pick up this loaded gun
----------------------------------

Bandaid assumes that you keep your site in Git or another VCS, and
will assume that anything worth keeping is either committed or
stashed. Consider yourself warned.

Installing
----------

The recommended way to install is using composer:

* [Install Composer globally](http://getcomposer.org/doc/00-intro.md#system-requirements) (if needed).

* To install the lastest stable:

    cd ~/.drush && composer require xendk/bandaid:*

* To install the bleeding edge:

    cd ~/.drush && composer require xendk/bandaid:dev-master

* To update (will update to the lastest stable or bleeding edge
  depending on what you chose initially):

    cd ~/.drush && composer update xendk/bandaid

Or you can install manuallly by cloning the repo or downloading a
release package into .drush, and running composer install in the
bandaid directory.

Usage
-----

Common options:

  `--no-cache`: will override the file download cache.

Commands, in the order they'll be useful:

### Patching ###

    drush bandaid-patch <patch file|url of patch|d.o issue> [project path]

Project path is the path to the project you want to patch. Optional if
you are issuing the command from the module's or project's directory.

Will patch the module with supplied patch, and if successful pop up
your editor for a reason for patching (to remind your future you why
you did this in the first place). This information will be written to
a .yml file next to the module directory. You can edit the YAML file
if the need be, but be aware that it's used by the following commands.

If the supplied patch is a local file, it will be saved to the YAML
file with a path relative to the YAML file, so it is assumed that
local patches are committed to the repository.

Example:

    drush bandaid-patch patches/panels-something.patch  sites/all/modules/contrib/panels

Use the given patch and save it in the .yml file as
`../../../patches/panels-something.patch`.

Example:

    drush bandaid-patch https://www.drupal.org/node/1985980#comment-8596585 sites/all/modules/contrib/panels

Will patch the module with the patch from the fifth comment (cid
8596585).

For issue URLs, the "home" of the patch is automatically set to the
issue URL, for URLs pointing directly to the patch/local files, it
will ask the you.

If supplied an issue URL that doesn't point to a specific comment,
it'll list the found patches and ask which to use.

If you don't like the interactive questions, these can be supplied
with the `--home` and `--reason` options.

You can use `--editor` to specify your preferred editor (or set
`$EDITOR` or `$VISUAL`), or use `--no-editor` to not invoke an editor
at all.

#### Failure mode ####

Could theoretically crap all over your module if the patch doesn't
apply properly. A subtle reminder of the good practice of committing
the original module first. However, as `git` and `patch` is pretty
conservative, it's unlikely they'd really foul up the module, and `git
reset` should be able to fix it anyway.

### Checking local changes ###

    drush bandaid-diff [project path] [patch file]

Example:

    drush bandaid-diff sites/all/modules/contrib/panels

Shows the diff of the local changes, minus the patches from the YAML
file. Can be used to examining the state of a module or producing
patches for upstream. Should produce a warning if the patch wouldn't
apply to the base revision.

Will output the patch on stdout unless the second arguments is given.

#### Failure mode ####

Won't do anything in case of error.

### Removing patches ###

    drush bandaid-tearoff [project path]

Example:

    drush bandaid-tearoff sites/all/modules/contrib/panels

Will reverse the applied patches, and create a `<module>.local.patch`
file that contains any further local modifications.

You can now use `drush dl` to upgrade the module. 

#### Failure mode ####

In the case that a patch from the YAML file doesn't apply cleanly, or
other errors, it'll just stubbornly refuse to do anything, leaving it
up to you to bisect your way to finding out whoever screwed up the
YAML file or updated the module without properly dealing with the YAML
file, and thus apply the clue stick upon.

For less drastic fouls (such as patches applied but not mentioned in
the YAML file), it'll just produce a local patch with more changes
than you'd expect.

### Re-patching ###

    drush bandaid-apply [project path]

Example:

    drush bandaid-apply sites/all/modules/contrib/panels

Will reapply the patches from the yaml file, and lastly any
`<module>.local.patch` and, if successful, delete the local patch file.

#### Failure mode ####

Will error out per default if any of the patches from the YAML file
fail to apply. The option `--ignore-failing` will make it ignore
failing patches and `--update-yaml` removes the patches from the YAML
file. Handy if the patches has been applied upstream.

You can also hack the YAML file manually and use this command to apply
a set of patches to a pristine version of the module. To ensure that
you've not left a mess for the next poor soul to come along, commit
your changes (to a temporary branch, if you prefer), and try running
bandaid-tearoff, and see if the local patch looks sane.

### De-gitting ###

    drush bandaid-degit [project path]

Example:

    drush bandaid-degit custom_module

If you have a project that is a git checkout, this command will make a
note of the origin repository and the checked out revision in the YAML
file.

#### Failure mode ####

As it deletes the .git directory, it *will* delete any un-committed
changes, un-pushed commits and stashes.

### Re-gitting ###

    drush bandaid-regit [project path]

Example:

    drush bandaid-regit custom_module

Will turn a project into a git repository. The origin and revision is
either read from the YAML file (where bandaid-degit put it), or can be
supplied with the `--origin` ande `--revision` command line options.

This is handy for pushing changes upstream or updating projects
downloaded via git.

#### Failure mode ####

Not being able to figure out the right origin and revision, which just
leaves you where you started.

In closing
----------

If you discover a module that produces crap in the local patches or
otherwise make Bandaid misbehave, open an issue.

If it breaks, you get to keep both pieces.
