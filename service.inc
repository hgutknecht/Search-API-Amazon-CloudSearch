<?php

/**
 * @file
 * service.inc
 * Implements the SearchApiAbstractService for CloudSearch
 */

class SearchApiCloudSearchService extends SearchApiAbstractService {

  /**
   * Returns CloudSearch specific settings to the Search API add/edit form.
   */
  public function configurationForm(array $form, array &$form_state) {
    if (!$this->isAwsSdkSetup()) {
      drupal_set_message(t('AWS SDK for PHP is not installed. You cannot proceed until the SDK is installed.  Please follow the instructions in README.txt.'), 'error');
      return;
    }

    $form['intro'] = array(
      '#type' => 'markup',
      '#markup' => '<p>' . t("Either link an existing CloudSearch domain or create a new one.  Start by entering you AWS account's credentials which can be found by going to the security area of the") . ' ' . l(t('AWS portal site.'), 'https://aws-portal.amazon.com/gp/aws/securityCredentials', array('attributes' => array('target' => '_blank'))) .
      t("<em>If you create a new domain Amazon will start charging your account as soon as it has loaded.  It's strongly recommended you check you AWS console after the domain is created to ensure it is setup to your satisfaction.") . l(t('Amazon CloudSearch Pricing'), 'http://aws.amazon.com/cloudsearch/pricing/', array('attributes' => array('target' => '_blank'))) . '</em></p>',
    );

    $form['aws_access_key'] = array(
      '#type' => 'textfield',
      '#title' => 'AWS Access Key',
      '#required' => TRUE,
      '#default_value' => empty($this->options) ? '' : $this->options['aws_access_key'],
      '#description' => t('The access key of the AWS account you would like to use.'),
      '#size' => 25,
    );
    $form['aws_secret_key'] = array(
      '#type' => 'textfield',
      '#title' => 'AWS Secret Key',
      '#required' => TRUE,
      '#default_value' => empty($this->options) ? '' : $this->options['aws_secret_key'],
      '#description' => t('The secret key of the AWS account you would like to use.'),
      '#size' => 50,
    );

    $form['region'] = array(
      '#type'  => 'select',
      '#title'  => t('Region'),
      '#required' => TRUE,
      '#options' => array('cloudsearch.us-east-1.amazonaws.com' => 'us-east-1','cloudsearch.eu-west-1.amazonaws.com' => 'eu-west-1'),
      '#description' => t('The region the current domain is in or where you would like to put the new domain.'),
      '#default_value' => empty($this->options) ? '' : $this->options['region'],
    );

    $form['domain_name'] = array(
      '#type'  => 'textfield',
      '#title'  => t('Search domain name'),
      '#required' => TRUE,
      '#maxlength' => 28,
      '#default_value' => empty($this->options) ? '' : $this->options['domain_name'],
    );

    if (empty($this->options)) {
      $form['create_new_domain'] = array(
        '#type'  => 'checkbox',
        '#title'  => t('Create new domain?'),
        '#description' => t('Checking this will create the above domain in your CloudSearch account. Amazon will start charging by the hour as soon as the domain is loaded.  For full pricing details visit') . ' ' . l(t('Amazon CloudSearch Pricing'), 'http://aws.amazon.com/cloudsearch/pricing/', array('attributes' => array('target' => '_blank'))),
      );

      // Get servers ip for doc and search endpoint access policy setup.
      $local_ip_address = $this->getExternalIPAddress();
      if ($local_ip_address == "ERROR") {
        drupal_set_message(t('Failed to find this servers public IP address. This is required by CloudSearch to restrict access to the document and search endpoints. Please fill in your SERVERS PUBLIC ip address in the field provided.  You can change/manage access policies within the AWS console.'), 'warning');
        $local_ip_address = "";
      }
      $form['access_ip'] = array(
        '#type'  => 'textfield',
        '#title'  => t('Access policy: IP address'),
        '#required' => FALSE,
        '#description' => t("CloudSearch restricts access to it's search and document endpoints to specific IP Addresses.  The pre-selected IP address is the best guess of this servers public IP address. Please change it if it is not correct.  Access policies can be managed from the AWS console."),
        '#maxlength' => 15,
        '#default_value' => $local_ip_address,
      );
    }

    $form['cloudsearch_api_version'] = array(
      '#type'  => 'textfield',
      '#title'  => t('API Version'),
      '#required' => TRUE,
      '#description' => t('Specify which API version should be used to communicate with CloudSearch.  Leave as default if unsure.'),
      '#default_value' => '2011-02-01',
    );

    $form['show_debug'] = array(
      '#type'  => 'checkbox',
      '#title'  => t('Show debug info'),
      '#description' => t('If checked the CloudSearch request and response will be shown when a search is performed. Note this is for development purposes has no access restrictions'),
      '#default_value' => empty($this->options) ? '' : $this->options['show_debug'],
    );

    return $form;
  }

  /**
   * Helpder - Get servers external IP Address.
   *
   * Rough and ready way of getting the external ip address of this server.
   */
  public function getExternalIPAddress() {
    $ch = curl_init("http://icanhazip.com/");
    curl_setopt($ch, CURLOPT_HEADER, FALSE);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, TRUE);
    $result = curl_exec($ch);
    curl_close($ch);
    if ($result === FALSE) {
      return "ERROR";
    }
    else {
      return trim($result);
    }
  }

  /**
   * Validation callback for the form returned by configurationForm().
   *
   * Does nothing by default.
   *
   * @param array $form
   *   The form returned by configurationForm().
   * @param array $values
   *   The part of the $form_state['values'] array corresponding to this form.
   * @param array $form_state
   *   The complete form state.
   */
  public function configurationFormValidate(array $form, array &$values, array &$form_state) {
    $domain_name = $form_state['values']['options']['form']['domain_name'];

    // Check domain.
    preg_match("/[a-z][a-z0-9-]+/", $domain_name, $domain_match);
    if (drupal_strlen($domain_name) < 3) {
      form_error($form['domain_name'], t('Domain name must be between 3 and 28 characters.'));
      return;
    }
    elseif (count($domain_match) > 0 && $domain_match[0] != $domain_name) {
      form_error($form['domain_name'], t('Domain name entered has some invalid chacters. Please check and try again.'));
      return;
    }

    if (!empty($values['access_ip']) && drupal_strlen($values['access_ip']) > 0 && $this->validIP($values['access_ip']) == FALSE) {
      form_error($form['access_ip'], t('The IP address was not valid, please check and try again.'));
      return;
    }


    $cloudsearch = AWSCloudSearchHelper::getAWSCloudSearchInstance($form_state['values']['options']['form']['aws_access_key'], $form_state['values']['options']['form']['aws_secret_key'], $form_state['values']['options']['form']['region']);

    $new = empty($form_state['values']['options']['form']['create_new_domain']) ? FALSE : $form_state['values']['options']['form']['create_new_domain'];

    if ($new == TRUE) {
      $response = $cloudsearch->describe_domains(array('DomainNames' => $domain_name));
    }
    else {
      $response = $cloudsearch->describe_domains(array('DomainNames' => $domain_name));
    }

    if (!$response->isOK()) {
      AWSCloudSearchHelper::checkAndReport(t('Retrieve domain details'), $response, FALSE);
      form_error($form['aws_access_key'], check_plain(t('Failed to request domain details from CloudSearch.') . ': ' . AWSCloudSearchHelper::getAWSError($response)));
      return;
    }

    if ($new == TRUE && count($response->body->DescribeDomainsResult->DomainStatusList->member) > 0) {
      form_error($form['aws_access_key'], t('You have selected to create a new domain but this domain already exists.'));
      return;
    }

    /* This isn't a new domain so check it actually exists and
     * raise an error if not.
     */
    if ($new == FALSE) {
      if (count($response->body->DescribeDomainsResult->DomainStatusList->member) == 0) {
        form_error($form['domain_name'], t('Search domain not found, please check your details and try again.  If you want to create a new domain then tick the Create new domain checkbox.'));
      }
      else {
        $form_state['storage']['search_endpoint'] = (string) $response->body->DescribeDomainsResult->DomainStatusList->member->SearchService->Endpoint;
        $form_state['storage']['document_endpoint'] = (string) $response->body->DescribeDomainsResult->DomainStatusList->member->DocService->Endpoint;
      }
    }
  }

  /**
   * Submit callback for the form returned by configurationForm().
   *
   * The default implementation just ensures that additional elements in
   * $options, not present in the form, don't get lost at the update.
   *
   * @param array $form
   *   The form returned by configurationForm().
   * @param array $values
   *   The part of the $form_state['values'] array corresponding to this form.
   * @param array $form_state
   *   The complete form state.
   */
  public function configurationFormSubmit(array $form, array &$values, array &$form_state) {

    if (empty($values['create_new_domain']) == FALSE && $values['create_new_domain'] == TRUE) {
      // Create new domain in Cloudsearch.
      $cloudsearch = AWSCloudSearchHelper::getAWSCloudSearchInstance($values['aws_access_key'], $values['aws_secret_key'], $values['region']);
      $response = $cloudsearch->create_domain($values['domain_name']);
      if (!$response->isOK()) {
        AWSCloudSearchHelper::checkAndReport(t('Create domain'), $response, TRUE);
        return;
      }
      $response = $cloudsearch->describe_domains(array('DomainNames' => $values['domain_name']));
      if (!$response->isOK()) {
        AWSCloudSearchHelper::checkAndReport(t('Retrieve domain'), $response, TRUE);
        return;
      }
      /* Set end points to constant as they will not
       * be available straight away.
       */
      $values['search_endpoint'] = "NEW";
      $values['document_endpoint'] = "NEW";

      unset($values['create_new_domain']);
      drupal_set_message(t('The CloudSearch domain was created successfully.  It will take a few minutes for the domain to fully initialise.  Please allow some and check the domain status is Active before indexing items.'));

    }
    else {
      $values['search_endpoint'] = $form_state['storage']['search_endpoint'];
      $values['document_endpoint'] = $form_state['storage']['document_endpoint'];
    }

    if (!empty($this->options)) {
      $values += $this->options;
    }
    $this->options = $values;

    if (!empty($values['access_ip']) && drupal_strlen($values['access_ip']) > 0) {
      $csdomain = new AWSCloudSearchDomain($values, NULL);
      $csdomain->addIPAccessPolicy($values['access_ip']);
    }
  }

  /**
   * Helper - returns true if valid IP address.
   */
  public function validIP($ip) {
    if (preg_match("^([1-9]|[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])(\.([0-9]|[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])){3}^", $ip)) {
      return TRUE;
    }
    else {
      return FALSE;
    }
  }

  /**
   * Determines whether this service class supports a given feature.
   *
   * Features are optional extensions to Search API functionality and
   * usually defined and used by third-party modules.
   *
   * There are currently three features defined directly in the Search API
   * project:
   * - "search_api_facets", by the search_api_facetapi module.
   * - "search_api_facets_operator_or", also by the search_api_facetapi module.
   * - "search_api_mlt", by the search_api_views module.
   * Other contrib modules might define additional features. These should always
   * be properly documented in the module by which they are defined.
   *
   * @param string $feature
   *   The name of the optional feature.
   *
   * @return boolean
   *   TRUE if this service knows and supports the specified feature. FALSE
   *   otherwise.
   */
  public function supportsFeature($feature) {
    return $feature == 'search_api_facets' || $feature == 'search_api_views' || $feature == 'search_api_sort' || $feature == 'search_api_facets_operator_or' || $feature == 'search_api_ranges';
  }

  /**
   * View this server's settings.
   *
   * Output can be HTML or a render array, a <dl>
   * listing all relevant settings is preferred.
   *
   * The default implementation does a crude output as a definition list, with
   * option names taken from the configuration form.
   */
  public function viewSettings() {
    $output = '';
    $form = $form_state = array();
    $option_form = $this->configurationForm($form, $form_state);
    $option_names = array();
    foreach ($option_form as $key => $element) {
      if (isset($element['#title']) && isset($this->options[$key])) {
        $option_names[$key] = $element['#title'];
      }
    }

    $output .= '<dt><strong>' . t('CloudSearch status') . '</strong></dt>' . "\n";
    $output .= '<dd><em>' . t('The following information was retrieved directly from AWS CloudSearch system.') . '</em></dd>' . "\n";
    $csdomain = new AWSCloudSearchDomain($this->options, NULL);
    $domain_status = $csdomain->getDomainDetails();

    // Check domain was found.
    if ($domain_status == FALSE) {
      $output .= '<dt class="error">' . t('Failed to retrieve domain details from AWS.') . '</dt>' . "\n";
    }
    else {
      $domain_details = get_object_vars($domain_status);
      $this->checkDomainLoaded(TRUE);

      $this->buildDomainDetails($domain_details, $domain_options);
      foreach ($domain_options as $key => $value) {
        $output .= '<dt>' . check_plain($key) . '</dt>' . "\n";
        $output .= '<dd>' . nl2br(check_plain(print_r($value, TRUE))) . '</dd>' . "\n";

      }
    }

    $output .= '<dt><strong>' . t('Drupal settings') . '</strong></dt>' . "\n";
    $output .= '<dd><em>' . t('Shows the drupal specific settings for this domain') . '</em></dd>' . "\n";

    foreach ($option_names as $key => $name) {
      $value = $this->options[$key];
      $output .= '<dt>' . check_plain($name) . '</dt>' . "\n";
      // Mask the access key.
      if ($key == 'aws_access_key' || $key == 'aws_secret_key') {
        $output .= '<dd>' . drupal_substr($value, 0, 5) . '****************** (' . t('Partially masked for security') . ") </dd>\n";
      }
      else {
        $output .= '<dd>' . nl2br(check_plain(print_r($value, TRUE))) . '</dd>' . "\n";
      }
    }

    return $output ? "<dl>\n$output</dl>" : '';
  }

  /**
   * Helper - Creates a single flat array out of a CloudSearch domain details.
   */
  public function buildDomainDetails($details, &$options, $suffix = NULL) {
    foreach ($details as $key => $name) {
      if (is_object($name)) {
        $this->buildDomainDetails(get_object_vars($details[$key]), $options, $key);
      }
      else {
        if ($suffix != NULL) {
          $options[$suffix . '  ' . $key] = $name;
        }
        else {
          $options[$key] = $name;
        }
      }
    }
  }

  /**
   * Add a new index to this server.
   *
   * @param SearchApiIndex $index
   *   The index to add.
   */
  public function addIndex(SearchApiIndex $index) {
    $this->checkDomainLoaded(TRUE);

    $csdomain = new AWSCloudSearchDomain($this->options, $index->machine_name);
    $csdomain->updateIndexes($index);
    drupal_set_message(t('CloudSearch indexes were updated. It can take up to 10 minutes for the indexes to be processed. During this time no items can be added to CloudSearch'));
  }

  /**
   * Updates fields/indexes in CloudSearch.
   */
  public function fieldsUpdated(SearchApiIndex $index) {
    $this->checkDomainLoaded(TRUE);

    $csdomain = new AWSCloudSearchDomain($this->options, $index->machine_name);
    $csdomain->updateIndexes($index);
  }

  /**
   * Adds entities into CloudSearch given the index and items array.
   */
  public function indexItems(SearchApiIndex $index, array $items) {
    $this->checkDomainLoaded(TRUE);

    $csdomain = new AWSCloudSearchDomain($this->options, $index->machine_name);

    if (!$csdomain->isActive()) {
      drupal_set_message(t('Domain currently not in a state to accept documents. This is likely to be because index fields have been updated and the domain is processing these changes which can take some time.'), 'warning');
      return array();
    }

    if ($csdomain->updateDocuments($items) == TRUE) {
      return array_keys($items);
    }
    else {
      drupal_set_message(t('Failed when calling CloudSearch document endpoint. See Reports >> Recent entries for more information.'), 'error');
      return array();
    }

  }

  /**
   * Deletes item out of index given array of ids or all if specified.
   */
  public function deleteItems($ids = 'all', SearchApiIndex $index = NULL) {
    $this->checkDomainLoaded(TRUE);

    if ($ids == 'all' && $index == NULL) {
      $ids = array();
      drupal_set_message(t('SearchAPI CloudSearch does not currently support the removal of all documents.'), 'warning');
    }
    $csdomain = new AWSCloudSearchDomain($this->options, $index->machine_name);
    if ($ids == 'all') {
      // Get all ids for index type.
      $query = new EntityFieldQuery();
      $result = $query
        ->entityCondition('entity_type', $index->item_type)
        ->execute();

      if (isset($result[$index->item_type])) {
        $csdomain->removeDocuments(array_keys($result[$index->item_type]));
      }
    }
    else {
      $csdomain->removeDocuments($ids);
    }

  }

  /**
   * Constructs and calls the CloudSearch search.
   */
  public function search(SearchApiQueryInterface $query) {
    $this->checkDomainLoaded(TRUE);
    $start_time = microtime(TRUE);
    $csdomain = new AWSCloudSearchQuery($this->options, $query->getIndex()->machine_name);
    $time_processing_done = microtime(TRUE);
    $results = $csdomain->execute($query);
    $end_time = microtime(TRUE);

    $results['performance'] = array(
      'complete' => $end_time - $start_time,
      'preprocessing' => $time_processing_done - $start_time,
      'execution' => $end_time - $time_processing_done,
      'postprocessing' => 0,
    );
    return $results;

  }

  /**
   * Helper - refreshes the document endpoints for this domain.
   */
  public function refreshEndpoints() {
    $csdomain = new AWSCloudSearchDomain($this->options, NULL);
    $status = $csdomain->getDomainDetails();

    if (isset($status->SearchService->Endpoint) && isset($status->DocService->Endpoint)) {
      $ops = $this->options;
      $ops['search_endpoint'] = (string) $status->SearchService->Endpoint;
      $ops['document_endpoint'] = (string) $status->DocService->Endpoint;
      $this->server->update(array('options' => $ops));
      return TRUE;
    }
    else {
      // Disable server if endpoints are available.
      $this->server->update(array(
        'enabled' => 0,));
      drupal_set_message(t('CloudSearch domain endpoints are currently unavailable. This is likely due to the domain having been recently created. It can take some time for the domain to be created.  This domain will not be enabled until the endpoints are available, this normally takes a few minutes.'), 'warning');
      return FALSE;
    }
  }

  /**
   * Helper - checks to see if the endpoint options are set.
   *
   * If not it will go to CloudSearch and check to see if the endpoints are
   * now configured. If they are it will update options.  If not it will
   * ensure this server is disabled and report.
   */
  public function checkDomainLoaded($refresh = FALSE) {

    if ($this->options['document_endpoint'] == 'NEW'
          || $this->options['search_endpoint'] == 'NEW'
            || $this->options['search_endpoint'] == ''
              || $this->options['search_endpoint'] == '') {
      if ($refresh == TRUE) {
        if ($this->refreshEndpoints() == TRUE) {
          drupal_set_message(t('CloudSearch domain endpoints are now available. This domain is fully active and ready to be configured'));
          return TRUE;
        }
      }
      return FALSE;
    }
    else {
      return TRUE;
    }
  }

  /**
   * Helper - Checks to see if AWS SDK for PHP is present.
   */
  public function isAwsSdkSetup() {
    $info = libraries_load('awssdk');
    return $info['installed'];
  }
}
