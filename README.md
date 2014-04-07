Bandaid
=======

Drush tool for helping with patch management on Drupal, which helps
with patching, and upgrading modules.

Before you pick up this loaded gun
----------------------------------

Bandaid assumes that you keep your site in Git or another VCS, and
will assume that anything worth keeping is either committed or
stashed. Consider yourself warned.

Usage
-----

Commands, in the order they'll be useful:

### Patching ###

    drush bandaid-patch <url of patch> <module>

Example:

    drush bandaid-patch https://drupal.org/files/issues/panels-new-pane-alter-1985980-5.patch panels

Will patch the module with the given patch, and if successful, ask for
the URL of the issue, and pop up your editor for a reason for patching
(to remind your future you why you did this in the first place). This
information will be written to a .yml file next to the module
directory. You can edit the yaml file if the need be, but be aware
that it's used by the following commands.

If you don't like the interactive questions, these can be supplied
with the --home and --reason options.

#### Failure mode ####

Will crap all over your module with if the patch doesn't apply. A
subtle reminder of the good practice of committing the original module first.

### Removing patches ###

    drush bandaid-tearoff <module>

Example:

    drush bandaid-tearoff panels

Will reverse the applied patches, and create a `<module>.local.patch`
file that contains any further local modifications.

You can now use `drush dl` to upgrade the module. 

#### Failure mode ####

In the case that a patch from the yaml file doesn't apply cleanly, or
other errors, it'll just stubbonly refuse to do anything, leaving it
up to you to bisect your way to finding out whoever screwed up the
yaml file or updated the module without properly dealing with the yaml
file, and thus apply the clue stick upon.

For less drastic fouls (such as patches applied but not mentioned in
the yaml file), it'll just produce a local patch with more changes
than you'd expect.

### Re-patching ###

    drush bandaid-apply <module>

Example:

    drush bandaid-apply panels

Will reapply the patches from the yaml file, and lastly any
`<module>.local.patch` and, if successful, delete the local patch file.

#### Failure mode ####

Craps out in case of error, leaving it up to you to deal with the
problem. The way to do this would be to manually apply each patch from
the yaml file, and replacing those who fail. Lastly, apply the local
patch, fixing up any conflicts.

To ensure that you've not left a mess for the next poor soul to come
along, commit your changes (to a temporary branch, if you prefer), and
try running bandaid-tearoff, and see if the local patch looks sane.

In closing
----------

If you discover a module that produces crap in the local patches, open
an issue. 

If it breaks, you get to keep both pieces.
