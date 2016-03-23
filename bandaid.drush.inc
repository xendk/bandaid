<?php

/**
 * @file
 * Bandaid for Drush.
 *
 * Warning: This is still much a proof of concept, with duplicate code, harcoded
 * values, missing documentation and lacking error messages. Those of a sensible
 * dispossition should avert their eyes.
 */

require 'bandaid.inc';

use Bandaid\Git;
use Bandaid\BandaidError;
use Bandaid\Project;
use Symfony\Component\Yaml\Yaml;
use Goutte\Client;
define('BANDAID_MINIMUM_PHP', '5.3.0');
// Cache for a week. Patches shouldn't change.
define('BANDAID_CACHE_LIFETIME_DEFAULT', 604800);

/**
 * Implements hook_drush_command().
 */
function bandaid_drush_command() {
  $local_vendor_path = __DIR__ . '/vendor/autoload.php';
  $global_vendor_path = __DIR__ . '/../../../vendor/autoload.php';

  // Check for a local composer install or a global composer install. Vendor
  // dirs are in different spots).
  if ((!@include $local_vendor_path) && (!@include $global_vendor_path)) {
    drush_log("Unable to load autoload.php. Bandaid requires Composer in order to install its depedencies and autoload classes.", 'warning');
    return array();
  }

  $items['bandaid-patch'] = array(
    'description' => 'Add a patch.',
    'arguments' => array(
      'patch' => 'URL of patch to apply.',
      'project' => 'Path to project to patch. If left out, current dir is patched.',
    ),
    'options' => array(
      'home' => 'URL of issue.',
      'reason' => 'Reason for patch.',
      'editor' => 'Editor command to use for reason if not given by --reason',
      'no-editor' => "Don't use an editor, prompt on command line instead.",
      'no-cache' => "Don't use patch download cache.",
      'info-file' => "Info file to use in case the module does not have one at its root. Specify relative to root, eg 'ldap_authentication/ldap_authentication.info'",
      'download' => 'Download the patch and store it locally',
      'download-destination' => 'Specifies the path the patch should be downloaded to, defaults to "sites/all/patches'
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('bp'),
  );

  $items['bandaid-diff'] = array(
    'description' => 'Show local diff.',
    'arguments' => array(
      'project' => 'Path to project to show diff for. Default to current dir.',
      'file' => 'File to save diff to, optional.',
    ),
    'options' => array(
      'no-cache' => "Don't use patch download and git repo cache.",
      'info-file' => "Info file to use in case the module does not have one at its root. Specify relative to root, eg 'ldap_authentication/ldap_authentication.info'",
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('bd'),
  );

  $items['bandaid-tearoff'] = array(
    'description' => 'Tear off patches.',
    'arguments' => array(
      'project' => 'Path to project to remove patches from. Default to current dir.',
    ),
    'options' => array(
      'no-cache' => "Don't use patch download and git repo cache.",
      'info-file' => "Info file to use in case the module does not have one at its root. Specify relative to root, eg 'ldap_authentication/ldap_authentication.info'",
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('bt'),
  );

  $items['bandaid-apply'] = array(
    'description' => 'Reapply patches.',
    'arguments' => array(
      'project' => 'Path to project to apply patches to. Default to current dir.',
    ),
    'options' => array(
      'no-cache' => "Don't use patch download and git repo cache.",
      'ignore-failing' => "Skip patches that fails to apply.",
      'update-yaml' => "Remove failed patches from YAML file.",
      'info-file' => "Info file to use in case the module does not have one at its root. Specify relative to root, eg 'ldap_authentication/ldap_authentication.info'",
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('ba'),
  );

  $items['bandaid-degit'] = array(
    'description' => 'De-Git a project.',
    'arguments' => array(
      'project' => 'Path to project to De-Git. If left out, current dir is used.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('bdg'),
  );

  $items['bandaid-regit'] = array(
    'description' => 'Re-Git a project.',
    'arguments' => array(
      'project' => 'Path to project to Re-Git. If left out, current dir is used.',
    ),
    'options' => array(
      'origin' => 'Repository URL, overrides repository from YAML file.',
      'revision' => 'Revision, overrides repository from YAML file.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'aliases' => array('brg'),
  );

  $items['bandaid-convert-from-patches-txt'] = array(
    'description' => 'Convert PATCHES.txt to OMG',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'arguments' => array(
      'project' => 'Path to project that contains a PATCHES.txt. Default to current dir.',
    ),
    'options' => array(
      'download' => 'Whether to download non drupal-org patches and store them.',
      'download-destination' => 'Path to the local directory to download the patch to.',
      'download-all' => 'Download all patches including patches originating on drupal.org.'
    ),
    'aliases' => array('bcp'),
  );

  return $items;
}

/**
 * Implements hook_drush_help().
 */
function bandaid_drush_help($section) {
  $help = NULL;
  switch ($section) {
    case 'meta:bandaid:title':
      $help = dt('Bandaid');
      break;

    case 'meta:bandaid:summary':
      $help = dt('Bandaid patch management.');
      break;

    case 'drush:bandaid-patch':
      $help = array(
        dt('Apply a patch.'),
        '',
        dt('Will download the patch and apply it to the project. Makes a note of it in the <project>.yml file.'),
      );
      break;

    case 'drush:bandaid-diff':
      $help = array(
        dt('Outputs the diff of the local modifications.'),
        '',
        dt('Attempts to remove the patches from the <project>.yml files from the project outputs a diff with any remaining changes.'),
      );
      break;

    case 'drush:bandaid-tearoff':
      $help = array(
        dt('Removes all patches from project.'),
        '',
        dt('Attempts to remove the patches from the <project>.yml files from the project and creates a <project>.local.patch file with any remaining changes.'),
      );
      break;

    case 'drush:bandaid-apply':
      $help = array(
        dt('Reapply patches.'),
        '',
        dt('Reapplies the patches from the <project.yml> file and any found
<project>.local.patch file to the project.'),
      );
      break;

    case 'drush:bandaid-degit':
      $help = array(
        dt('De-Git project.'),
        '',
        dt('Makes a note of the repository origin and checked out revision in the YAML file, and deletes the .git directory.'),
        '',
        dt('This is handy for committing projects not from Drupal.org.'),
      );
      break;

    case 'drush:bandaid-regit':
      $help = array(
        dt('Re-Git project.'),
        '',
        dt('Makes the project a git checkout.'),
        '',
        dt('Handy for pushing local changes to the project upstream.'),
      );
      break;
  }
  return $help;
}

/**
 * Command callback.
 *
 * Download and apply a patch to a project.
 */
function drush_bandaid_patch($patch = NULL, $project = NULL) {
  try {
    if (!$patch) {
      throw new BandaidError('NO_PATCH', dt('What do you suppose I should patch WITH?'));
    }
    _bandaid_check_php_version();

    $project = new Project($project);
    // Override info file if set on command line.
    if ($info_file = drush_get_option('info-file', NULL)) {
      $project->setInfoFileName($info_file);
    }
    $cwd = getcwd();

    // Translate to patch URL if pointing at an issue.
    $patch = _bandaid_patch_from_issue($patch);

    $tmp_patch_path = _bandaid_get_file($patch);

    chdir($project->getPath());
    $patched = _bandaid_attempt_patch($tmp_patch_path);
    chdir($cwd);

    if (!$patched) {
      throw new BandaidError('PATCHING_FAILED', dt('Could not apply patch.'));
    }
    drush_log(dt('Successfully patched.'));
    $new_patch = array(
      'patch' => $patch,
    );

    $home = drush_get_option('home', NULL);
    if (is_null($home)) {
      $home = drush_prompt(dt("Issue (or other web page) of patch?"), NULL, FALSE);
    }
    if (!empty($home)) {
      $new_patch['home'] = $home;
    }

    $reason = drush_get_option('reason', NULL);
    if (is_null($reason)) {
      if (!drush_get_option('no-editor', FALSE)) {
        $editor = drush_get_editor();
        $reason_file_name = drush_tempnam('bandaid_');
        $reason_content = "

# Patch: $patch" . (!empty($home) ? "\n# Home: " . $home : '') . "
# Enter your explanation for adding this patch above.
# Lines staring with # will be removed";
        file_put_contents($reason_file_name, $reason_content);
        drush_shell_exec_interactive($editor, $reason_file_name);
        $reason = explode("\n", file_get_contents($reason_file_name));
        $reason = array_filter($reason, function ($line) {
          return $line[0] != '#';
        }
        );
        $reason = trim(implode("\n", $reason));
      }
      else {
        // Fall back to using drush_prompt.
        $reason = drush_prompt(dt("Reason for patching?"), NULL, FALSE);
        if (!empty($reason)) {
          $new_patch['reason'] = $reason;
        }
      }
    }

    $new_patch['reason'] = $reason;

    // See if we should keep the patch locally.
    if (drush_get_option('download', FALSE)) {
      $destination_path = drush_get_option('download-destination',
        'sites/all/patches');

      // Prepare the destination directory and generate a unique filename.
      $local_patch_path = _drush_bandaid_prepare_local_patch_path($patch, $destination_path);

      if (!copy($tmp_patch_path, $local_patch_path)) {
        throw new BandaidError('PATCHING_FAILED', dt('Could not copy the downloaded patch to the destination-path !local_patch_path.', array('!local_patch_path' => $local_patch_path)));
      }

      $new_patch['patch'] = $local_patch_path;
    }

    $project->addPatch($new_patch);
    $project->save();
  }
  catch (BandaidError $e) {
    return $e->drushSetError();
  }
}


/**
 * Ensures the local patch directory exists and returns a verified path to it.
 *
 * @param $patch_url
 *   URL or path to the original patch.
 *
 * @param string $destination_path
 *   Path to the local patch directory.
 *
 * @return string
 *   Full destination-path to the patch.
 */
function _drush_bandaid_prepare_local_patch_path($patch_url, $destination_path) {
  // Add drupal-root to the path if it is not absolute.
  if (strpos($destination_path, '/') !== 0) {
    $site_path = drush_get_context('DRUSH_SELECTED_DRUPAL_ROOT');

    if (empty($site_path)) {
      throw new BandaidError('PATCHING_FAILED',
        dt('Download destination is relative (!destination) but could not resolve Drupal root.',
          array('!destination' => $destination_path)));
    }
    else {
      $destination_path = $site_path . DIRECTORY_SEPARATOR . $destination_path;
    }
  }

  // Make sure that the destination directory either exists or can be created.
  if (!file_exists($destination_path) && !@mkdir($destination_path, 0777, TRUE)) {
    throw new BandaidError('PATCHING_FAILED',
      dt('Download destination (!destination) did not exist and could not create the directory.',
        array('!destination' => $destination_path)));
  }

  // We have the full path to the directory, now make sure we have a unique
  // filename
  $patch_filename = _bandaid_get_patch_filename($patch_url);
  if (empty($patch_filename)) {
    throw new BandaidError('PATCHING_FAILED', dt('Could not determine patch filename from !url.',
        array('!url' => $patch_url)));
  }

  // We have the original filename, now en ensure it is valid in the destination
  // -directory, if not make it so.
  $patch_path_info = pathinfo($patch_filename);
  $counter = 0;
  $download_destination_full = $destination_path . DIRECTORY_SEPARATOR . $patch_path_info['filename'] . '.' . $patch_path_info['extension'];
  while (file_exists($download_destination_full)) {
    if ($counter > 1000) {
      throw new BandaidError('PATCHING_FAILED',
        dt('!filename already exists at destination !destination, failed to create a new unique name.',
          array(
            '!destination' => $destination_path,
            '!filename' => $patch_path_info['filename']
          )));
    }

    $download_destination_full = $destination_path . DIRECTORY_SEPARATOR . $patch_path_info['filename'] . '_' . $counter . '.' . $patch_path_info['extension'];
    $counter++;
  }

  return $download_destination_full;
}

/**
 * Command callback.
 *
 * Remove patches from project.
 */
function drush_bandaid_tearoff($project = NULL) {
  try {
    _bandaid_check_php_version();

    $project = new Project($project);
    // Override info file if set on command line.
    if ($info_file = drush_get_option('info-file', NULL)) {
      $project->setInfoFileName($info_file);
    }
    chdir($project->getPath());

    if ($project->hasLocalPatch()) {
      drush_log(dt('Local patch file exists, it will be overwritten.'), 'warning');
      if (!drush_confirm('Continue anyway?')) {
        return drush_user_abort();
      }
    }

    list($checkout_dir, $checkout_ref, $patch) = _bandaid_clone_and_tearoff($project);

    // Return to a pristine checkout.
    Git::checkout($project->getRevision());

    if (!$project->isCore() && $project->isFromDrupalOrg()) {
      // Generate a version-string like d.o and drush would do, if
      // there was one before.
      _bandaid_inject_info_file_metadata($checkout_dir, $project->getName(), $project->getInfoVersion());

      // Create a commit we can checkout.
      Git::add('.');
      Git::commit('Updated info.');
    }

    $sha = Git::sha();
    if (empty($sha)) {
      // Not much intelligent we can tell the user.
      throw new BandaidError('ERROR_GETTING_SHA', dt('Internal Git error.'));
    }

    // Check out what we have locally, this means that Git is aware of the files
    // in the local branch, and it can remove files that's been added locally.
    Git::checkout('bandaid_local');

    // Update the local files by making Git use it as work-tree, and checking
    // out.
    Git::resetTo($project->getPath(), TRUE);
    Git::checkoutTo($project->getPath(), $sha);

    // Create local patch file.
    if (!empty($patch)) {
      if (!file_put_contents($project->getLocalPatchFileName(), $patch)) {
        throw new BandaidError('CANNOT_CREATE_PATCH', dt('Error creating file @patch_file', array('@patch_file' => $project['local_patch_file'])));
      }

      // Make sure the yaml file is up to date.
      $project->save();
    }
  }
  catch (BandaidError $e) {
    return $e->drushSetError();
  }
}

/**
 * Command callback.
 *
 * Show diff of local modifications to a project.
 */
function drush_bandaid_diff($project = NULL, $file = NULL) {
  try {
    _bandaid_check_php_version();

    $cwd = getcwd();
    $project = new Project($project);
    // Override info file if set on command line.
    if ($info_file = drush_get_option('info-file', NULL)) {
      $project->setInfoFileName($info_file);
    }
    chdir($project->getPath());

    list($checkout_dir, $checkout_ref, $patch) = _bandaid_clone_and_tearoff($project);

    // Create local patch file.
    if (!empty($patch)) {
      if ($file) {
        chdir($cwd);
        file_put_contents($file, $patch);
        drush_log(dt('Saved to @file.', array('@file' => $file)), 'ok');
      }
      else {
        drush_print($patch);
      }
    }

    chdir($checkout_dir);
    // Return to a pristine checkout.
    Git::checkout($project->getRevision());

    if (!empty($patch)) {
      // Re-apply any local changes.
      $filename = drush_save_data_to_temp_file($patch);
      if (!Git::applyCheck($filename, 1)) {
        drush_log(dt('Patch does not apply cleanly to base revision.'), 'warning');
      }

      // We had changes, indicate it via exit-code.
      drush_set_context('DRUSH_ERROR_CODE', 1);
    }
  }
  catch (BandaidError $e) {
    return $e->drushSetError();
  }
}

/**
 * Command callback.
 *
 * Reapplies patches to project.
 */
function drush_bandaid_apply($project = NULL) {
  try {
    _bandaid_check_php_version();

    $project = new Project($project);
    // Override info file if set on command line.
    if ($info_file = drush_get_option('info-file', NULL)) {
      $project->setInfoFileName($info_file);
    }
    chdir($project->getPath());

    // Clone repo.
    drush_log(dt('Cloning @name.', array('@name' => $project->getName())), 'status');
    $checkout_dir = _bandaid_tmpdir();
    $clone_info = _bandaid_clone_from_info($project, $checkout_dir);
    drush_register_file_for_deletion(realpath($checkout_dir));

    chdir($checkout_dir);

    // Create local branch.
    Git::branch('bandaid_local');

    // Copy the local project to the local branch.
    Git::checkout('bandaid_local');

    // Add project ignore files.
    $ignores = $project->getIgnores();
    if ($ignores) {
      Git::addIgnores($ignores);
    }
    // Add the local version.
    Git::addFrom($project->getPath(), '.', TRUE);

    if ($project->isFromDrupalOrg()) {
      // Ignore any changes to LICENSE.txt. Drupal.org automatically adds it.
      if (file_exists($project->getPath() . '/LICENSE.txt')) {
        Git::reset(FALSE, 'LICENSE.txt');
      }
    }

    // Update repo work tree to what we just added.
    Git::checkout('.');

    // Remove extra info added by drupal.org/drush/ourselves from the info
    // files, to not pollute diff.
    if ($project->isFromDrupalOrg()) {
      _bandaid_fix_info_files($checkout_dir);
    }

    // Commit local version.
    Git::add('.', TRUE);
    Git::commit("Committing local.", TRUE);

    // Create a diff to show the user.
    $patch = Git::diff($clone_info['revision']);
    if (!empty($patch)) {
      drush_log(dt('Local changes detected, these will be lost if continuing.'), 'warning');
      drush_print($patch);
      if (!drush_confirm('Continue anyway?')) {
        return drush_user_abort();
      }
    }

    // Reset so we get the state back to the same as the local version.
    Git::reset(TRUE);

    // Get the sha for the unmodified version. We'll need it later.
    $sha = Git::sha();
    if (empty($sha)) {
      // Not much intelligent we can tell the user.
      throw new BandaidError('ERROR_GETTING_SHA', dt('Internal Git error.'));
    }

    // Apply patches to local branch.
    $patches = $project->getPatches();
    if ($patches) {
      $new_patches = _bandaid_patch($patches);
    }

    // Apply a local patch.
    $patch_file = $project->getLocalPatchFileName();
    if (file_exists($patch_file)) {
      if (Git::apply($patch_file)) {
        drush_log(dt('Patched with @filename.', array('@filename' => basename($patch_file))), 'ok');
        unlink($patch_file);
        // Create a commit we can checkout.
        Git::add('.');
        Git::commit('Added local patch.');
      }
      else {
        drush_log(dt('Error applying local patch. Please apply it manually.'), 'error');
      }
    }

    // Check out the unmodified local version to the repo.
    Git::checkout($sha);

    // Update the local files by making Git use it as work-tree, and checking
    // out.
    Git::checkoutTo($project->getPath(), 'bandaid_local');

    // If some patches were ignored, write new yaml file.
    if (($new_patches != $patches) && drush_get_option('update-yaml', FALSE)) {
      $project->setPatches($new_patches);
      $project->save();
      drush_log('Updated yaml file.', 'ok');
    }
  }
  catch (BandaidError $e) {
    return $e->drushSetError();
  }
}

/**
 * Command callback.
 *
 * De-Git a project.
 */
function drush_bandaid_degit($project = NULL) {
  try {
    _bandaid_check_php_version();

    $project = new Project($project);
    // Override info file if set on command line.
    if ($info_file = drush_get_option('info-file', NULL)) {
      $project->setInfoFileName($info_file);
    }
    chdir($project->getPath());

    if (!file_exists('.git')) {
      throw new BandaidError('NOT_A_REPOSITORY', dt('@path is not a Git repository.', array('@path' => $project->getPath())));
    }

    drush_log(dt('This will lose stashes and non-pushed commits/branches from this checkout.'), 'warning');

    if (!drush_confirm('Continue anyway?')) {
      return drush_user_abort();
    }

    $project->setOrigin(Git::origin());
    $project->setRevision(Git::sha());

    // Write the YAML file.
    $project->save();
    drush_op('drush_delete_dir', '.git', TRUE);
    if (file_exists('.git')) {
      throw new BandaidError('COULD_NOT_DELETE', dt("Couldn't delete .git dir. Check the permissions?"));
    }
  }
  catch (BandaidError $e) {
    return $e->drushSetError();
  }
}

/**
 * Command callback.
 *
 * Re-Git a project.
 */
function drush_bandaid_regit($project = NULL) {
  try {
    _bandaid_check_php_version();

    $project = new Project($project);
    // Override info file if set on command line.
    if ($info_file = drush_get_option('info-file', NULL)) {
      $project->setInfoFileName($info_file);
    }
    chdir($project->getPath());

    if (file_exists('.git')) {
      throw new BandaidError('ALREADY_A_REPOSITORY', dt('@path is already a Git repository.', array('@path' => $project ? $project : basename(cwd()))));
    }

    if ($origin = drush_get_option('origin', NULL)) {
      $project->setOrigin($origin);
    }

    if ($revision = drush_get_option('revision', NULL)) {
      $project->setRevision($revision);
    }

    // Clone into a temporary directory.
    $checkout_dir = _bandaid_tmpdir();
    _bandaid_clone($project->getOrigin(), $checkout_dir);
    drush_register_file_for_deletion(realpath($checkout_dir));

    chdir($checkout_dir);
    Git::checkout($project->getRevision());

    rename(realpath($checkout_dir . '/.git'), $project->getPath() . '/.git');
  }
  catch (BandaidError $e) {
    return $e->drushSetError();
  }
}

/**
 * Check that PHP is high enough a version.
 */
function _bandaid_check_php_version() {
  if (version_compare(phpversion(), BANDAID_MINIMUM_PHP) < 0) {
    throw new BandaidError('BANDAID_REQUIREMENTS_ERROR', dt('Your command line PHP installation is too old. Bandaid requires at least PHP !version.', array('!version' => DRUSH_MINIMUM_PHP)));
  }
  return TRUE;
}

/**
 * Get a patch from an d.o issue.
 */
function _bandaid_patch_from_issue($url) {
  if (preg_match('{drupal.org/node/\\d+(#comment-(\\d+))?}', $url, $rx)) {
    drush_log(dt("Looks like a Drupal issue url, looking for patches."), 'status');
    $patches = _bandaid_get_patches_from_issue($url);
    if (empty($patches)) {
      throw new BandaidError('NO_PATCH', dt('No patches found on issue.'));
    }

    // If the URL points to a comment, try to find that specific patch.
    if (!empty($rx[2])) {
      if (isset($patches[$rx[2]])) {
        $selected_patch = $patches[$rx[2]];
      }
      else {
        throw new BandaidError('NO_PATCH', dt('No patch found on comment @cid.', array('@cid' => $rx[2])));
      }
    }
    else {
      if (count($patches) > 1) {
        $options = array();
        foreach ($patches as $issue_patch) {
          $options[$issue_patch['cid']] = '#' . $issue_patch['num'] . ' ' . $issue_patch['href'];
        }
        $selected = drush_choice($options, dt("Please select patch."));
        if (isset($patches[$selected])) {
          $selected_patch = $patches[$selected];
        }
        else {
          return drush_user_abort();
        }
      }
      else {
        drush_log(dt("Found one patch."), 'status');
        $selected_patch = reset($patches);
      }
    }

    if ($selected_patch) {
      // Use the issue URL as home, if it is not already set.
      if (!drush_get_option('home', NULL)) {
        drush_set_option('home', $url);
      }
      $url = $selected_patch['href'];
    }
  }

  return $url;
}

/**
 * Parse a version string.
 *
 * @todo The datestamp parameter indicates that this needs to be rethought.
 */
function _bandaid_parse_version($version, $datestamp = NULL) {
  // Possibilities: 7.x-1.4 7.x-1.4+3-dev 7.x-2.0-alpha8+33-dev 7.x-1.x-dev
  if (preg_match('{^(?P<core>\\d\\.x)-(?P<major>\\d+)\\.((?P<minor>x|\\d+(?:-[a-z]+\\d+)?)(?:\\+(?P<commits>\\d+)-dev)?)}', $version, $matches)) {
    $version_info = array(
      'core' => $matches['core'],
      'version' => $matches['major'] . '.' . $matches['minor'],
      'major' => $matches['major'],
      'commits' => '',
      'datestamp' => '',
    );
    if ($matches['minor'] == 'x') {
      if ($datestamp) {
        $version_info['datestamp'] = $datestamp;
      }
      else {
        throw new BandaidError('RAW_DEV_NOT_SUPPORTED', dt('Dev releases without a patch level or datestamp is not supported.'));
      }
    }
    if ($matches['commits']) {
      $version_info['commits'] = $matches['commits'];
    }
  }
  // Detect a SHA, we accept sha's down to 12 chars, seems to be an accepted
  // minimum for the linux kernel https://lkml.org/lkml/2010/10/28/287.
  elseif (preg_match('/^(?P<sha>[a-f0-9]{12,40})$/', $version, $matches)) {
    $version_info = array('sha' => $matches['sha']);
  }
  else {
    throw new BandaidError('BAD_VERSION', dt('Could not parse version "@version"', array('@version' => $version)));
  }

  return $version_info;
}

/**
 * Clones project and does a tearoff.
 *
 * Returns path to clone, base ref and local patch contents.
 */
function _bandaid_clone_and_tearoff($project) {
  // Clone repo.
  drush_log(dt('Cloning @name.', array('@name' => $project->getName())), 'status');
  $checkout_dir = _bandaid_tmpdir();

  _bandaid_clone_from_info($project, $checkout_dir);
  drush_register_file_for_deletion($checkout_dir);

  chdir($checkout_dir);

  // Create patch branch.
  Git::branch('bandaid_patch');

  // Create local branch.
  Git::branch('bandaid_local');

  // Apply patches to patch branch.
  $patches = $project->getPatches();
  if ($patches) {
    Git::checkout('bandaid_patch');
    _bandaid_patch($patches);
  }

  // Copy the local project to the local branch.
  Git::checkout('bandaid_local');

  // Add project ignore files.
  $ignores = $project->getIgnores();
  if ($ignores) {
    Git::addIgnores($ignores);
  }
  // Add the local version.
  Git::addFrom($project->getPath(), '.', TRUE);

  if ($project->isFromDrupalOrg()) {
    // Ignore any changes to LICENSE.txt. Drupal.org automatically adds it.
    if (file_exists($project->getPath() . '/LICENSE.txt')) {
      Git::reset(FALSE, 'LICENSE.txt');
    }
  }

  // Update repo work tree to what we just added.
  Git::checkout('.');
  // And clean out any deleted files.
  Git::clean();

  if ($project->isFromDrupalOrg()) {
    _bandaid_fix_info_files($checkout_dir);
  }

  Git::add('.', TRUE);
  Git::commit("Committing local.", TRUE);

  // Create local patch.
  $patch = Git::diff('bandaid_patch', 'bandaid_local');

  return array($checkout_dir, $project->getRevision(), $patch);
}

/**
 * Creates and returns the name of a temporary directory.
 */
function _bandaid_tmpdir() {
  $system_tmp = rtrim(drush_find_tmp(), '/');
  $tmp_name = 'bandaid_tmp_' . time() . '_' . uniqid();
  $tmp_dir = $system_tmp . '/' . $tmp_name;
  if (!drush_mkdir($tmp_dir)) {
    throw new BandaidError('CANNOT_CREATE_TEMP_DIR', dt('Cannot create temporary directory "@dir". Please check permissions for @tmp.', array('@dir' => $tmp_name, '@tmp' => $system_tmp)));
  }
  drush_register_file_for_deletion($tmp_dir);
  return $tmp_dir;
}

/**
 * Clone project using info from info/YAML file.
 */
function _bandaid_clone_from_info($project, $path) {
  // Clone the repo.
  _bandaid_clone($project->getOrigin(), $path);

  $cwd = getcwd();
  chdir($path);

  $revision = $project->getRevision($path);
  $message = $project->getMessage($revision, $path);
  // Checkout base version.
  drush_log(dt('Checking out @ref.', array('@ref' => $revision)), 'status');
  if (!empty($message)) {
    drush_log(dt('Commit message: @desc.', array('@desc' => $message)), 'status');
  }
  Git::checkout($revision);

  chdir($cwd);
}

/**
 * Clone a repo, with caching.
 *
 * Doesn't check any revision out initially.
 *
 * Uses the same cache as Drush make, code mostly lifted from makes
 * implementation.
 */
function _bandaid_clone($repo, $path) {
  $cache = !drush_get_option('no-cache', FALSE);
  $name = basename($repo, '.git');
  if ($cache && ($git_cache = drush_directory_cache('git'))) {
    $project_cache = $git_cache . '/' . $name . '-' . md5($repo);
    // Set up a new cache, if it doesn't exist.
    if (!file_exists($project_cache)) {
      $options = array(
        'mirror' => TRUE,
      );
      if (drush_get_context('DRUSH_VERBOSE')) {
        $options += array(
          'verbose' => TRUE,
          'progress' => TRUE,
        );
      }
      Git::cloneRepository($repo, $project_cache, $options);
    }
    else {
      // Update the --mirror clone.
      $dir = getcwd();
      chdir($project_cache);
      Git::remoteUpdate();
      chdir($dir);
    }
    $git_cache = $project_cache;
  }

  $options = array(
    // Don't check anything out initially.
    'no-checkout' => TRUE,
  );
  if (drush_get_context('DRUSH_VERBOSE')) {
    $options += array(
      'verbose' => TRUE,
      'progress' => TRUE,
    );
  }
  if ($cache) {
    $options['reference'] = $git_cache;
  }

  try {
    Git::cloneRepository($repo, $path, $options);
  }
  catch (BandaidError $e) {
    // Throw a new Exception so we can get the project name in.
    throw new BandaidError('CANNOT_CLONE', dt('Unable to clone @project from @url.', array('@project' => $name, '@url' => $repo)));
  }
  return TRUE;
}

/**
 * Attempt to patch with either git or patch.
 *
 * Will output the command output in verbose mode.
 */
function _bandaid_attempt_patch($filename) {
  $patched = FALSE;
  // Test each patch style; -p1 is the default with git. See
  // http://drupal.org/node/1054616
  $patch_levels = array(1, 0);
  foreach ($patch_levels as $patch_level) {
    $checked = Git::applyCheck($filename, $patch_level);
    if ($checked) {
      // Apply the first successful style.
      $patched = Git::apply($filename, $patch_level);
      break;
    }
  }

  // In some rare cases, git will fail to apply a patch, fallback to using
  // the 'patch' command.
  if (!$patched) {
    foreach ($patch_levels as $patch_level) {
      // --no-backup-if-mismatch here is a hack that fixes some
      // differences between how patch works on windows and unix.
      if ($patched = drush_shell_exec("patch -p%s --no-backup-if-mismatch < %s", $patch_level, $filename)) {
        break;
      }
    }
  }

  if ($output = drush_shell_exec_output()) {
    // Log any command output, visible only in --verbose or --debug mode.
    drush_log(implode("\n", $output));
  }

  return $patched;
}

/**
 * Apply patches to project.
 *
 * @todo Handle local files.
 */
function _bandaid_patch($patches) {
  $processed_patches = array();
  foreach ($patches as $patch) {
    // Download the patch.
    $filename = _bandaid_get_file($patch['patch']);

    // Set up string placeholders to pass to dt().
    $dt_args = array(
      '@filename' => basename($filename),
    );

    $patched = _bandaid_attempt_patch($filename);

    if ($patched) {
      $processed_patches[] = $patch;

      drush_log(dt('Patched with @filename.', $dt_args), 'ok');
      $message = "Patched with " . $patch['patch'];
      if (!empty($patch['reason'])) {
        drush_log(dt('(@reason)', array('@reason' => $patch['reason'])), 'ok');
        $message = "\n\n" . $patch['reason'];
      }
      Git::add('.', TRUE);
      Git::commit($message);
    }
    else {
      if (drush_get_option('ignore-failing', FALSE)) {
        drush_log(dt("Unable to patch with @filename, skipping.", $dt_args), 'error');
      }
      else {
        throw new BandaidError('PATCH_ERROR', dt("Unable to patch with @filename.", $dt_args));
      }
    }
  }
  return $processed_patches;
}

/**
 * Download a file, if necessary.
 *
 * Originally stolen from Drush make. Will return the realpath of local files.
 *
 * Don't remove the file after use, this is done automatically for downloaded
 * files, and local files shouldn't be removed.
 *
 * @param string $download
 *   The url of the file to download.
 * @param int $cache_duration
 *   The time in seconds to cache the resultant download.
 *
 * @return string
 *   The location of the downloaded file, or FALSE on failure.
 */
function _bandaid_get_file($download, $cache_duration = BANDAID_CACHE_LIFETIME_DEFAULT) {
  if (parse_url($download, PHP_URL_SCHEME) == NULL) {
    $file = realpath($download);
    if (file_exists($file)) {
      return $file;
    }
    throw new BandaidError('COULD_NOT_FIND_FILE', dt('File @url does not exist.', array('@url' => $download)));
  }
  if (drush_get_option('no-cache', FALSE)) {
    $cache_duration = 0;
  }

  $tmp_path = _bandaid_tmpdir();
  // Ensure that we aren't including the querystring when generating a filename
  // to save our download to.
  $file = basename(current(explode('?', $download, 2)));
  $tmp_file = drush_download_file($download, $tmp_path . '/' . $file, $cache_duration);
  if (!$tmp_file) {
    throw new BandaidError('COULD_NOT_DOWNLOAD_FILE', dt('Could not download @url', array('@url' => $download)));
  }
  drush_register_file_for_deletion($tmp_file);
  return $tmp_file;
}

/**
 * Parses an url or path to a patch and returns the filename.
 */
function _bandaid_get_patch_filename($url_or_path) {
  if (!$path = parse_url($url_or_path, PHP_URL_PATH)) {
    return FALSE;
  }

  return pathinfo($path, PATHINFO_BASENAME);
}

/**
 * Removes d.o/drush/bandaid metadata from info files.
 */
function _bandaid_fix_info_files($dir) {
  $info_files = drush_scan_directory($dir, '/.*\.info$/');
  foreach ($info_files as $info_file) {
    $content = file_get_contents($info_file->filename);
    $lines = explode("\n", $content);
    for ($i = count($lines) - 1; $i > -1; $i--) {
      // We'll take any packaging script name. Drupal.org has on at least one
      // occasion managed to produce "...added by  packaging script...".
      if (preg_match('/^\s*;\s+Information added by .* (packaging script|on)/', $lines[$i], $matches)) {
        break;
      }
    }
    if ($i > -1) {
      // Drush adds two new lines before the metadata whereas drupal.org just
      // adds one line.
      if (strstr($matches[0], 'drush')) {
        $i--;
      }
      $lines = array_slice($lines, 0, $i);
      if (!file_put_contents($info_file->filename, implode("\n", $lines))) {
        throw new BandaidError('CANNOT_WRITE_INFO', dt('Could not write info file @file', array('@file' => $info_file->filename)));
      }
    }
  }
  return TRUE;
}

/**
 * Inject metadata into all .info files for a given project.
 *
 * Just like d.o and drush does.
 *
 * @param string $project_dir
 *   The full path to the root directory of the project to operate on.
 * @param string $project_name
 *   The project machine name (AKA shortname).
 * @param string $version
 *   The version string to inject into the .info file(s).
 *
 * @return bool
 *   TRUE on success, FALSE on any failures appending data to .info files.
 */
function _bandaid_inject_info_file_metadata($project_dir, $project_name, $version) {
  $info_files = drush_scan_directory($project_dir, '/.*\.info$/');
  if (!empty($info_files)) {
    $info = "\n; Information added by bandaid on " . date('Y-m-d') . "\n";
    $info .= "version = \"$version\"\n";
    $matches = array();
    if (preg_match('/^((\d+)\.x)-.*/', $version, $matches) && $matches[2] >= 6) {
      $info .= "core = \"$matches[1]\"\n";
    }
    // @todo should add this option.
    if (!drush_get_option('no-gitprojectinfo', FALSE)) {
      $info .= "project = \"$project_name\"\n";
    }
    $info .= 'datestamp = "' . time() . "\"\n";
    $info .= "\n";
    foreach ($info_files as $info_file) {
      if (!drush_file_append_data($info_file->filename, $info)) {
        return FALSE;
      }
    }
  }
  return TRUE;
}

/**
 * Extract patch urls from a Drupal issue URL.
 */
function _bandaid_get_patches_from_issue($url) {
  $client = new Client();
  $crawler = $client->request('GET', $url);
  $patches = $crawler->filter('#extended-file-field-table-field-issue-files tr')->each(function ($row) {
      $cid = 0;
      $num = 'node';
      $x = $row->filter('td.extended-file-field-table-cid a');
      if (count($x)) {
        if (preg_match('/^\\s*#(\\d+)\\s*$/', $x->text(), $rx)) {
          $num = $rx[1];
        }
        if (preg_match('/#comment-(\\d+)$/', $x->attr('href'), $rx)) {
          $cid = $rx[1];
        }
      }
      $hrefs = $row->filter('td.extended-file-field-table-filename a')->each(function ($node){
                 return $node->attr('href');
               });
      if ($hrefs[0]) {
        return array(
          'num' => $num,
          'cid' => $cid,
          'href' => $hrefs[0],
        );
      }
    });
  $return = array();
  // Index the patches.
  foreach ($patches as $patch) {
    if (!empty($patch['num'])) {
      $return[$patch['cid']] = $patch;
    }
  }
  ksort($return);
  return $return;
}

/**
 * Parse a PATCHES.txt and return a list of patch urls.
 *
 * @param string $patches_txt_path path to the file to parse.
 *
 * @return array
 *   Array of patches, empty if none where found, FALSE if the file could not
 *   be read.
 */
function _drush_bandaid_parse_patchestxt($patches_txt_path) {
  $lines = file($patches_txt_path);

  // Explicitly return FALSE if the file could not be read.
  if ($lines === FALSE) {
    return FALSE;
  }

  $patches = array();
  // Find all lines starting with "- " and assume it is a patch.
  foreach ($lines as $line) {
    if (preg_match('|^- (.*)|', $line, $matches)) {
      $patches[] = trim($matches[1]);
    }
  }

  return $patches;
}

/**
 * Convert PATCHES.txt to OMG YAML files.
 */
function drush_bandaid_convert_from_patches_txt($project_or_patches_path = NULL) {
  // Strip off PATCHES.txt from the path.
  $project_path = preg_replace('|/PATCHES.txt$|', '', $project_or_patches_path);

  $project = new Project($project_path);

  // No path ... the module probably doesn't exist.
  if (!$project->getPath()) {
    drush_log(dt("Unable to load project named !project", array('!project' => $project->getName() )), 'warning');
    return;
  }

  chdir($project->getPath());
  $patches_txt_path = $project->getPath() . DIRECTORY_SEPARATOR . 'PATCHES.txt';

  if (!is_readable('PATCHES.txt')) {
    drush_log(dt("Could not read patch-status from !path", array('!path' => $patches_txt_path )), 'warning');
    return;
  }

  $patches = _drush_bandaid_parse_patchestxt($patches_txt_path);
  if (empty($patches_txt_path)) {
    drush_log(dt("unable to parse !path", array('!path' => $patches_txt_path )), 'warning');
    return;
  }

  $added_patches = array();
  foreach ($patches as $patch_url) {
    $patch = array();

    // Fix old style drupal.org links to point to https and www.drupal.org.
    $patch_url = preg_replace('|^(https?://(www\.)?drupal\.org/)|', 'https://www.drupal.org/', $patch_url);

    // Skip if patch is already added.
    foreach ($project->getPatches() as $existing_patch)  {
      if ($existing_patch['patch'] == $patch_url) {
        break 2;
      }
    }

    drush_log(dt('- adding !patch', array('!patch' => $patch_url)), 'ok');

    // See if the patch should be stored locally.
    if ($download = drush_get_option('download', FALSE)) {
      // Download drupal.org patch (or all if configured to.)
      if (drush_get_option('download-all', FALSE) || strpos($patch_url, 'https://www.drupal.org/') === FALSE) {
        $destination_path = drush_get_option('download-destination', 'sites/all/patches');

        // Prepare the destination directory and generate a unique filename.
        $local_patch_path = _drush_bandaid_prepare_local_patch_path($patch_url, $destination_path);

        $tmp_patch_path = _bandaid_get_file($patch_url);
        if (!copy($tmp_patch_path, $local_patch_path)) {
          throw new BandaidError('PATCHING_FAILED', dt('Could not copy the downloaded patch to the destination-path !local_patch_path.', array('!local_patch_path' => $local_patch_path)));
        }
        drush_log(dt('  - Downloaded to !destination', array(
          '!destination' => $local_patch_path)
        ), 'ok');

        // Work on the local path from now on.
        $patch_url = $local_patch_path;
      }
    }

    $patch['patch'] = $patch_url;
    $patch['reason'] = 'Converted from PATCHES.txt';

    // For d.o. issues lookup title in API and use for reason.
    if (preg_match('|^https://www\.drupal\.org/files/.*-(\d+)-\d+\.patch|', $patch_url, $matches) ||
      preg_match('|^https://www\.drupal\.org/files/(\d+)-.*\.patch|', $patch_url, $matches)) {
      drush_log(dt('  - patch-url seems to contain a issue-number, attempting to fetch info from drupal.org', array('!patch' => $patch_url)), 'ok');
      $issue = json_decode(file_get_contents("https://www.drupal.org/api-d7/node/${matches[1]}.json"));

      if (!empty($issue->url)) {
        drush_log(dt('  - Issue url: !url', array('!url' => $issue->url)), 'ok');
        $patch['home'] = $issue->url;
      }

      if (!empty($issue->title)) {
        drush_log(dt('  - Reason: !reason', array('!reason' => ucfirst($issue->title))), 'ok');

        $reason = 'Issue title: ' . ucfirst($issue->title);
        $patch['reason'] = $reason;
      }
    }

    $added_patches[] = $patch_url;
    $project->addPatch($patch);
  }

  if (!empty($added_patches)) {
    $project->save();

    drush_log(dt('Added !count patch(es) to !project', array('!count' => count($added_patches), '!project' => $project->getName())), 'ok');
  }
}
