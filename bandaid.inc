<?php

/**
 * @file
 * Helper classes for Bandaid.
 */

namespace Bandaid;

use Symfony\Component\Yaml\Yaml;

/**
 * Bandaid error class.
 */
class BandaidError extends \Exception {
  protected $error;

  /**
   * Constuctor.
   */
  public function __construct($error, $message = '', $previous = NULL) {
    $this->error = $error;
    parent::__construct($message, 0, $previous);
  }

  /**
   * Set error.
   *
   * Calls drush_set_error().
   */
  public function drushSetError() {
    if (($previous = $this->getPrevious()) && ($previous instanceof BandaidError)) {
      $previous->drushSetError();
    }
    // The error has already been set.
    if ($this->error == 'drush_set_error') {
      return FALSE;
    }
    if ($this instanceof BandaidInternalError &&
      !drush_get_context('DRUSH_DEBUG')) {
      // Don't print internal error messages unless in debug mode.
      $message = dt('Internal error.');
    }
    else {
      $message = !empty($this->message) ? $this->message : NULL;
    }
    return drush_set_error($this->error, $message);
  }
}

/**
 * Error class for internal errors.
 */
class BandaidInternalError extends BandaidError {}

/**
 * Encapsulates Bandaids git operations.
 */
class Git {
  /**
   * Run a command.
   */
  protected static function exec($command, $args = array(), $error_code = 'GIT_EXEC_ERROR', $error_msg = "Error executing command \"!command\"") {
    array_unshift($args, $command);
    $result = call_user_func_array('drush_shell_exec', $args);
    array_shift($args);
    if (!$result) {
      throw new BandaidError($error_code, strtr($error_msg, array('!command' => vsprintf($command, $args))));
    }
    return drush_shell_exec_output();
  }

  /**
   * Get the sha for the ref or HEAD.
   */
  public static function sha($ref = 'HEAD') {
    $sha = static::exec('git rev-parse %s', array($ref));
    return trim($sha[0]);
  }

  /**
   * Get the origin.
   *
   * Attempts to tease out the origin of git remote -v.
   */
  public static function origin() {
    $lines = static::exec('git remote -v');
    $origin = NULL;
    foreach ($lines as $line) {
      if (preg_match('{^origin\s+(.*)\s+\(push\)$}', $line, $matches)) {
        $origin = $matches[1];
      }
    }

    if (!$origin) {
      throw new BandaidError(CANNOT_FIND_ORIGIN, 'Cannot figure out origin. Sorry.');
    }
    return $origin;
  }

  /**
   * Check if a patch will apply.
   *
   * @return bool
   *   Whether git thinks it will apply.
   */
  public static function applyCheck($filename, $level = 1) {
    try {
      static::exec('GIT_DIR=. git apply --check -p%s %s --verbose', array($level, $filename));
      return TRUE;
    }
    catch (BandaidError $e) {
      return FALSE;
    }
  }

  /**
   * Apply a patch.
   *
   * @return bool
   *   TRUE if the patch was applied successfully.
   */
  public static function apply($filename, $level = 1) {
    try {
      static::exec('GIT_DIR=. git apply -p%s %s --verbose', array($level, $filename));
      return TRUE;
    }
    catch (BandaidError $e) {
      return FALSE;
    }
  }

  /**
   * Get the oneline log for one or more commits.
   */
  public static function onelineLog($start, $end = NULL) {
    if (empty($end)) {
      return static::exec('git log --format=oneline -1 %s', array($start), 'CANNOT_GET_HISTORY');
    }
    else {
      return static::exec('git log --format=oneline %s..%s', array($start, $end), 'CANNOT_GET_HISTORY');
    }
  }

  /**
   * Checkout branch/tag/sha.
   */
  public static function checkout($ref) {
    return static::exec('git checkout %s', array($ref), 'CANNOT_CHECKOUT');
  }

  /**
   * Checkout branch/tag/sha to a work-tree.
   */
  public static function checkoutTo($work_tree, $ref) {
    return static::exec('git --work-tree=%s checkout %s', array($work_tree, $ref), 'CANNOT_CHECKOUT');
  }

  /**
   * Create a branch.
   */
  public static function branch($name) {
    return static::exec('git branch %s', array($name), 'CANNOT_BRANCH');
  }

  /**
   * Add files to index.
   */
  public static function add($pattern, $all = FALSE) {
    $all = $all ? '--all' : '';
    return static::exec('git add %s %s', array($all, $pattern), 'CANNOT_ADD');
  }

  /**
   * Add files to index, from another work-tree.
   */
  public static function addFrom($work_tree, $pattern, $all = FALSE) {
    $args = array($work_tree, $all ? '--all' : '', $pattern);
    return static::exec('git --work-tree=%s add %s %s', $args, 'CANNOT_ADD');
  }

  /**
   * Commit changes.
   */
  public static function commit($message, $allow_empty = FALSE) {
    $allow_empty = $allow_empty ? '--allow-empty' : '';
    return static::exec('git commit %s -m %s', array($allow_empty, $message), 'CANNOT_COMMIT');
  }

  /**
   * Return a diff.
   */
  public static function diff($ref1, $ref2 = '') {
    // Just grabbing it from stdout doesn't work as drush <= 6 uses exec, which
    // strips trailing whitespace. Using a temp file gets around that problem.
    $temp_file = drush_tempnam('bandaid_diff_');
    static::exec('git diff %s %s > %s', array($ref1, $ref2, $temp_file), 'CANNOT_DIFF');
    return file_get_contents($temp_file);
  }

  /**
   * Reset workspace.
   *
   * @todo fix up the signature.
   */
  public static function reset($hard = FALSE, $path = '') {
    $hard = $hard ? '--hard' : '';
    // The double dash is rather important.
    return static::exec('git reset %s -- %s', array($hard, $path), 'CANNOT_RESET');
  }

  /**
   * Reset workspace in another work-tree.
   *
   * @todo fix up the signature.
   */
  public static function resetTo($work_tree, $hard = FALSE, $path = '') {
    $args = array($work_tree, $hard ? '--hard' : '', $path);
    // The double dash is rather important.
    return static::exec('git --work-tree=%s reset %s -- %s', $args, 'CANNOT_RESET');
  }

  /**
   * Clone repository.
   */
  public static function cloneRepository($repository, $path, $options = array()) {
    $opt = array();
    foreach ($options as $name => $value) {
      if ($value === TRUE || $value === NULL) {
        $opt[] = '--' . $name;
      }
      else {
        $opt[] = '--' . $name . ' ' . drush_escapeshellarg($value);
      }
    }
    $command = 'git clone ' . implode(' ', $opt) . ' %s %s';
    return static::exec($command, array($repository, $path), 'CANNOT_CLONE', dt('Unable to clone from @url.', array('@url' => $repository)));
  }

  /**
   * Run remote update.
   */
  public static function remoteUpdate() {
    static::exec('git remote update', array(), 'ERROR_UPDATING');
  }

  /**
   * Add ignores to repository.
   */
  public static function addIgnores($ignores) {
    $exclude_file = '.git/info/exclude';
    $existing_ignores = explode("\n", file_get_contents($exclude_file));
    $ignores = array_merge($existing_ignores, $ignores);
    array_unique($ignores);
    file_put_contents($exclude_file, implode("\n", $ignores));
  }
}

/**
 * Encapsulates project logic.
 */
class Project {
  /**
   * The path to the project.
   */
  protected $path;

  /**
   * Loaded YAML data.
   */
  protected $yaml = NULL;

  /**
   * Whether we loaded a legacy YAML file.
   *
   * We've changed the extension.
   */
  protected $yamlLegacy = FALSE;

  /**
   * Whether we've detected that this project is core.
   */
  protected $core = FALSE;

  /**
   * Repository origin.
   */
  protected $origin = NULL;

  /**
   * Originally detected repository origin.
   */
  protected $detectedOrigin = NULL;

  /**
   * Repository revision.
   */
  protected $revision = NULL;

  /**
   * Originally detected repository revision.
   */
  protected $detectedRevision = NULL;

  /**
   * Parsed info file data.
   */
  protected $versionInfo = NULL;

  /**
   * Original version from the info file.
   */
  protected $infoVersion = NULL;

  /**
   * Constructor.
   *
   * @param string $path
   *   Path to project.
   */
  public function __construct($path = NULL) {
    if (!$path) {
      $path = getcwd();
    }
    $this->path = realpath($path);

    if (!$this->loadYaml() && !file_exists($this->getInfoFileName())) {
      // If we found neither YAML or a default info file, see if we have core.
      $this->detectCore();
      $this->loadYaml();
    }
  }

  /**
   * Get path of project.
   */
  public function getPath() {
    return $this->path;
  }

  /**
   * Return the project name.
   *
   * We're somewhat guessing here.
   */
  public function getName() {
    if ($this->core) {
      return 'drupal';
    }

    return basename($this->path);
  }

  /**
   * Set info file override.
   *
   * @todo should it check for file existence?
   */
  public function setInfoFileName($name) {
    $this->yaml['info-file'] = $name;
  }

  /**
   * Path to the info file.
   */
  public function getInfoFileName() {
    if (isset($this->yaml['info-file'])) {
      return $this->yaml['info-file'];
    }
    return basename($this->path . '.info');
  }

  /**
   * Path to the YAML file.
   */
  public function getYamlFileName() {
    return $this->auxFileBase() . '.omg.yml';
  }

  /**
   * Legacy path to the YAML file.
   */
  public function getLegacyYamlFileName() {
    return $this->auxFileBase() . '.yml';
  }

  /**
   * Get path to the local patch.
   */
  public function getLocalPatchFileName() {
    return $this->auxFileBase() . '.local.patch';
  }

  /**
   * Whether there's an existing local patch.
   */
  public function hasLocalPatch() {
    return file_exists($this->getLocalPatchFileName());
  }

  /**
   * Get the list of patches applied.
   */
  public function getPatches() {
    if (!empty($this->yaml['patches'])) {
      return $this->prepareLocalPaths($this->yaml['patches']);
    }

    return array();
  }

  /**
   * Set list of patches.
   */
  public function setPatches($patches = array()) {
    $this->yaml['patches'] = array();
    foreach ($patches as $patch) {
      $this->addPatch($patch);
    }
  }

  /**
   * Add patch to project.
   */
  public function addPatch($patch) {
    // We're not requiring 'home', as it doesn't really make sense for patches
    // stored locally.
    if (!isset($patch['patch']) ||
      !isset($patch['reason'])) {
      throw new BandaidInternalError('BAD_PATCH', dt('Bad patch passed to addPatch.'));
    }

    if (!isset($this->yaml['patches'])) {
      $this->yaml['patches'] = array();
    }

    // If pointing to a local file, check that it exists and adjust the path
    // to be local to the yaml file.
    if (parse_url($patch['patch'], PHP_URL_SCHEME) == NULL) {
      $patch['patch'] = realpath($patch['patch']);
      if (!file_exists($patch['patch'])) {
        throw new BandaidInternalError('NONEXISTENT_PATCH', dt('Non-existent file passed to addPatch.'));
      }
      $patch['patch'] = $this->relativePath($this->auxFileBasePath(), $patch['patch']);
    }
    $this->yaml['patches'][] = $patch;
  }

  /**
   * Get files that git should ignore.
   *
   * @todo should do better than hardcoding D7 ignores.
   */
  public function getIgnores() {
    if ($this->core) {
      list($major, $minor) = explode('.', $this->core, 2);
      if ($major == 7) {
        return array(
          // Legacy handling. core.omg.yml used to be core.yml.
          'core.yml',
          'core.omg.yml',
          'core.local.patch',
          'sites/all/modules',
          '!sites/all/modules/README.txt',
          'sites/all/themes',
          '!sites/all/themes/README.txt',
          'sites/default',
          '!sites/default/default.settings.php',
          'profiles',
          '!profiles/minimal',
          '!profiles/standard',
          '!profiles/testing',
        );
      }
    }
    return array();
  }

  /**
   * Whether this is in fact core.
   */
  public function isCore() {
    return $this->core;
  }

  /**
   * Whether this is cloned from drupal.org.
   */
  public function isFromDrupalOrg() {
    // @todo Very simple-minded, but it'll do for now.
    return (strstr($this->getOrigin(), 'drupal.org') !== FALSE);
  }

  /**
   * Get the origin repository.
   */
  public function getOrigin() {
    $this->ensureOrigin();
    if (empty($this->origin)) {
      throw new BandaidError('NO_ORIGIN', dt('I have no idea where to clone this from.'));
    }
    return $this->origin;
  }

  /**
   * Set the origin repository.
   *
   * @param string $origin
   *   New origin url.
   *
   * @todo validation?
   */
  public function setOrigin($origin) {
    $this->origin = $origin;
  }

  /**
   * Get the origin revision.
   *
   * @param string $checkout
   *   The path to the checked out repository, in case we need to
   *   examine history.
   */
  public function getRevision($checkout = NULL) {
    $this->ensureRevision($checkout);
    if (empty($this->revision)) {
      throw new BandaidError('NO_REVISION', dt('I have no idea what revision to check out.'));
    }
    return $this->revision;
  }

  /**
   * Set the origin revision.
   *
   * @param string $revision
   *   The new revision.
   */
  public function setRevision($revision) {
    $this->revision = $revision;
  }

  /**
   * Get the message associated with ref.
   */
  public function getMessage($ref, $checkout = NULL) {
    if ($checkout) {
      $cwd = getcwd();
      chdir($checkout);
      $commits = array_reverse(Git::onelineLog($ref));
      chdir($cwd);
      list($dummy, $message) = explode(' ', reset($commits), 2);
      return $message;
    }

    return NULL;
  }

  /**
   * Get the version defined in the info file.
   */
  public function getInfoVersion() {
    $this->ensureVersionInfo();
    if (empty($this->infoVersion)) {
      throw new BandaidError('NO_INFO_VERSION', dt('No version found in info file.'));
    }
    return $this->infoVersion;
  }
  /**
   * Write the YAML file.
   */
  public function save() {
    $this->updateUpstream();

    // Prepare the output. Only switch to inline format at level 4.
    $new_yaml = Yaml::dump($this->yaml, 4, 2);

    // If we loaded from a legacy YAML, remove it.
    if ($this->yamlLegacy && !empty($this->yaml)) {
      $legacyname = $this->getLegacyYamlFileName();
      if (file_exists($legacyname)) {
        drush_log(dt("Renaming YAML file from .yml to .omg.yml"), 'warning');
        unlink($legacyname);
      }

    }
    // Delete the file if we have no content for it.
    $filename = $this->getYamlFileName();
    if (empty($this->yaml)) {
      if (file_exists($filename)) {
        unlink($filename);
      }
    }

    $current_yaml = FALSE;
    // Get the current contents of the file.
    if (file_exists($filename)) {
      $current_yaml = file_get_contents($filename);
    }

    // Write the yaml if we have any new content.
    if ($new_yaml != $current_yaml && !file_put_contents($filename, $new_yaml)) {
      throw new BandaidError('CANNOT_WRITE_YAML', dt('Error writing new YAML file @file', array('@file' => $filename)));

    }
  }

  /**
   * Update the YAML data with current origin/revision, if needed.
   */
  protected function updateUpstream() {
    if ($this->detectedOrigin != $this->origin ||
        $this->detectedRevision != $this->revision) {
      if (!isset($this->yaml['project'])) {
        $this->yaml['project'] = array();
      }
      $this->yaml['project']['type'] = 'git';
      $this->yaml['project']['origin'] = $this->origin;
      $this->yaml['project']['revision'] = $this->revision;
    }
    else {
      if (isset($this->yaml['project'])) {
        unset(
          $this->yaml['project']['type'],
          $this->yaml['project']['origin'],
          $this->yaml['project']['revision']
        );
        if (empty($this->yaml['project'])) {
          unset($this->yaml['project']);
        }
      }
    }
  }

  /**
   * Base name of our aux files..
   */
  protected function auxFileBase() {
    if ($this->core) {
      return $this->path . '/core';
    }
    return $this->path;
  }

  /**
   * Base path of our aux files..
   */
  protected function auxFileBasePath() {
    return dirname($this->auxFileBase());
  }

  /**
   * Load YAML file.
   */
  protected function loadYaml() {
    $yamlfile = $this->getYamlFileName();
    $legacyname = $this->getLegacyYamlFileName();
    if (file_exists($yamlfile)) {
      $this->yaml = Yaml::parse(file_get_contents($yamlfile));
      return TRUE;
    }
    elseif (file_exists($legacyname)) {
      $this->yaml = Yaml::parse(file_get_contents($legacyname));
      $this->yamlLegacy = TRUE;
      return TRUE;
    }

    return FALSE;
  }

  /**
   * Detect whether the project is in fact Drupal core.
   */
  protected function detectCore() {
    // Detect whether we're running on core.
    $version_files = array(
      "/modules/system/system.module",
      "/includes/bootstrap.inc",
      "/core/lib/Drupal.php",
    );
    foreach ($version_files as $version_file) {
      if (file_exists($this->path . $version_file)) {
        $file_contents = file_get_contents($this->path . $version_file);
        if (preg_match("{(?:define\\('VERSION',|const VERSION =) +'([^']+)'}", $file_contents, $matches)) {
          $this->core = $matches[1];
          drush_log(dt('Core @version detected.', array('@version' => $this->core)), 'status');
        }
      }
    }
  }

  /**
   * Ensure we've figured out the Git origin for this project.
   */
  protected function ensureOrigin() {
    if ($this->origin) {
      // Already parsed.
      return;
    }

    if ($this->validateProjectType()) {
      if (empty($this->yaml['project']['origin'])) {
        throw new BandaidError('NO_PROJECT_ORIGIN', dt('No origin repository in YAML file."'));
      }
      $this->origin = $this->yaml['project']['origin'];
    }
    else {
      if ($this->core) {
        $project = 'drupal';
      }
      else {
        $this->ensureVersionInfo();
        $project = $this->versionInfo['project'];
        if (empty($project)) {
          throw new BandaidError('NO_PROJECT', dt('No "project" in info file.'));
        }
      }
      $this->origin = 'http://git.drupal.org/project/' . $project;
      $this->detectedOrigin = $this->origin;
    }

  }

  /**
   * Make sure we've figured out the revision to check out.
   */
  protected function ensureRevision($checkout = NULL) {
    $this->ensureOrigin();
    if ($this->revision) {
      // Already parsed.
      return;
    }

    if ($this->validateProjectType()) {
      if (empty($this->yaml['project']['revision'])) {
        throw new BandaidError('NO_PROJECT_REVISION', dt('No origin revision in YAML file."'));
      }
      $this->revision = $this->yaml['project']['revision'];
    }
    else {
      $this->ensureVersionInfo();
      if (!empty($this->versionInfo['sha'])) {
        $this->revision = $this->versionInfo['sha'];
      }
      elseif (empty($this->versionInfo['version'])) {
        throw new BandaidError('NO_VERSION', dt('No "version" in info file.'));
      }
      $prefix = $this->core ? '' : $this->versionInfo['core'] . '-';
      $revision = $prefix . $this->versionInfo['version'];
      if ($this->versionInfo['commits']) {
        // If we don't have a checkout to examine, bomb out.
        if (!$checkout) {
          return;
        }

        $branch = 'origin/' . $prefix . $this->versionInfo['major'] . '.x';
        $tag    = $prefix . $this->versionInfo['version'];
        $cwd = getcwd();
        chdir($checkout);
        // Get history from base version to head.  Flip the order so we have the
        // oldest commit first.
        $commits = array_reverse(Git::onelineLog($tag, $branch));
        chdir($cwd);
        // Pick out the nth commit, and split the line.
        $split = explode(' ', $commits[$this->versionInfo['commits'] - 1], 2);
        $revision = $split[0];
      }
      $this->revision = $revision;
      $this->detectedRevision = $this->revision;
    }
  }

  /**
   * Validate the project data from YAML is wellformed.
   */
  protected function validateProjectType() {
    if (isset($this->yaml['project']['type'])) {
      if ($this->yaml['project']['type'] !== 'git') {
        throw new BandaidError('UNKNOWN_PROJECT_TYPE', dt('Unknown project type in YAML file: "@type"', array('@type' => $this->yaml['project']['type'])));
      }
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Ensure the info file has been loaded.
   */
  protected function ensureVersionInfo() {
    if (!$this->versionInfo) {
      if ($this->core) {
        $this->versionInfo = array(
          'version' => $this->core,
        );
      }
      else {
        $filename = $this->getInfoFileName();
        if (!file_exists($this->path . '/' . $filename)) {
          throw new BandaidError('NO_INFO_FILE', dt('Could not find the info file "@filename"', array('@filename' => $filename)));
        }
        $info = drush_drupal_parse_info_file($filename);
        if (!$info) {
          throw new BandaidError('COULD_NOT_PARSE_INFO_FILE', dt('Error parsing the info file.'));
        }

        $parsed_info = array();
        if (!empty($info['version'])) {
          $this->infoVersion = $info['version'];
          $parsed_info = _bandaid_parse_version($info['version']);
        }

        if (!empty($info['project'])) {
          $parsed_info['project'] = $info['project'];
        }
        $this->versionInfo = $parsed_info;
      }
    }
  }

  /**
   * Get a relative path from one file/dir to another.
   *
   * This implementation is borrowed from php.net.
   *
   * @param string $from
   *   The file to derive the relative path from.
   * @param string $to
   *   The file to get the relative path to.
   * @param string $ps
   *   (optional) Directory separator.
   *
   * @return string
   *   The relative path from $from to $to.
   *
   * @see http://dk1.php.net/manual/en/function.realpath.php#105876
   */
  protected function relativePath($from, $to, $ps = DIRECTORY_SEPARATOR) {
    $ar_from = explode($ps, rtrim($from, $ps));
    $ar_to = explode($ps, rtrim($to, $ps));
    while (count($ar_from) && count($ar_to) && ($ar_from[0] == $ar_to[0])) {
      array_shift($ar_from);
      array_shift($ar_to);
    }
    return str_pad('', count($ar_from) * 3, '..' . $ps) . implode($ps, $ar_to);
  }

  /**
   * Make any relative local file-system paths absolute.
   */
  private function prepareLocalPaths($patches) {
    foreach ($patches as $patch_id => &$patch_info) {
      // Only process the path if it is not currently a valid URL.
      if (parse_url($patch_info['patch'], PHP_URL_SCHEME) == NULL) {
        $absolute_path = realpath($this->auxFileBasePath() . DIRECTORY_SEPARATOR . $patch_info['patch']);
        // Only update the path if we can see a file there.
        if (is_file($absolute_path)) {
          $patch_info['patch'] = $absolute_path;
        }
      }
    }
    return $patches;
  }
}
