<?php

/**
 * @file
 * service.inc
 * Implements the SearchApiAbstractService for CloudSearch
 *  
*/


class SearchApiCloudSearchService extends SearchApiAbstractService {
  
  public function configurationForm(array $form, array &$form_state) {
    $form['intro'] = array(
      '#type' => 'markup', 
      '#markup' => '<p>' . t('Add a CloudSearch domain to the site.  You can either link an existing CloudSearch domain or create a new one.  Start by entering you AWS account\'s credentials which can be found by going to the security area of the ' . l(t('AWS portal site.'), 'https://aws-portal.amazon.com/gp/aws/securityCredentials', array('attributes' => array('target' => '_blank')))) . '</p>', 
    );
    
    $form['aws_access_key'] = array(
      '#type' => 'textfield', 
      '#title' => 'AWS Access Key', 
      '#required' => TRUE, 
      '#default_value' => empty($this->options) ? '' : $this->options['aws_access_key'], 
      '#size' => 25, 
    );
    $form['aws_secret_key'] = array(
      '#type' => 'textfield', 
      '#title' => 'AWS Secret Key', 
      '#required' => TRUE, 
      '#default_value' => empty($this->options) ? '' : $this->options['aws_secret_key'], 
      '#size' => 50, 
    );

    $form['region'] = array(
          '#type'  => 'select', 
          '#title'  => t('Region'), 
          '#required' => TRUE, 
          '#options' => array('cloudsearch.us-east-1.amazonaws.com' => 'us-east-1'), 
          '#default_value' => empty($this->options) ? '' : $this->options['region'], 
    );         
   
    $form['domain_name'] = array(
          '#type'  => 'textfield', 
          '#title'  => t('Search domain name'), 
          '#required' => TRUE, 
          '#default_value' => empty($this->options) ? '' : $this->options['domain_name'], 
    );
    
    $form['show_debug'] = array(
          '#type'  => 'checkbox', 
          '#title'  => t('Show debug info'), 
          '#default_value' => empty($this->options) ? '' : $this->options['show_debug'], 
    );
    
    $form['cloudsearch_api_version'] = array(
      '#type'  => 'textfield', 
      '#title'  => t('API Version'), 
      '#required' => TRUE, 
      '#default_value' => '2011-02-01', 
    );
    
    return $form;
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
    
     $cloudsearch = AWSCloudSearchHelper::getAWSCloudSearchInstance_Manual($form_state['values']['options']['form']['aws_access_key'], $form_state['values']['options']['form']['aws_secret_key'], $form_state['values']['options']['form']['region']);
     $response = $cloudsearch->describe_domains(array('DomainNames' => $form_state['values']['options']['form']['domain_name']));
     if (count($response->body->DescribeDomainsResult->DomainStatusList->member) == 0) {
        form_error($form['domain_name'], t('Search domain not found, please check your details and try again'));
     }
    
     $form_state['storage']['search_endpoint'] = (string)$response->body->DescribeDomainsResult->DomainStatusList->member->SearchService->Endpoint;
     $form_state['storage']['document_endpoint'] = (string)$response->body->DescribeDomainsResult->DomainStatusList->member->DocService->Endpoint;
     
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
    
    if (!empty($this->options)) {
      $values += $this->options;
      
    }
    $values['search_endpoint'] = $form_state['storage']['search_endpoint'];
    $values['document_endpoint'] = $form_state['storage']['document_endpoint'];
    
    $this->options = $values;
    
  }

  /**
   * Determines whether this service class implementation supports a given
   * feature. Features are optional extensions to Search API functionality and
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
   return $feature == 'search_api_facets' || $feature == 'search_api_views' || $feature == 'search_api_sort';
  }

  /**
   * View this server's settings. Output can be HTML or a render array, a <dl>
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
    
    $csdomain = new AWSCloudSearchDomain($this->options, NULL);
    $domain_details = get_object_vars($csdomain->get_domain_status());
    //var_dump($domain_details);
    //die();
    $this->build_domain_details($domain_details, $domain_options);
    foreach ($domain_options as $key => $value) {
      $output .= '<dt>' . check_plain($key) . '</dt>' . "\n";
      $output .= '<dd>' . nl2br(check_plain(print_r($value, TRUE))) . '</dd>' . "\n";  
      
    }
    
    $output .= '<dt><strong>' . t('Drupal settings') . '</strong></dt>' . "\n";
    
    foreach ($option_names as $key => $name) {
      $value = $this->options[$key];
      $output .= '<dt>' . check_plain($name) . '</dt>' . "\n";
      if ($key == 'aws_access_key' || $key == 'aws_secret_key') {
        $output .= '<dd>' . substr($value, 0, 6) . '****************** (' . t('Partially masked for security') . ") </dd>\n";
      } 
      else {
        $output .= '<dd>' . nl2br(check_plain(print_r($value, TRUE))) . '</dd>' . "\n";  
      }
    }

    return $output ? "<dl>\n$output</dl>" : '';
  }

  public function build_domain_details($details, &$options) {
    foreach ($details as $key => $name) {
      if (is_object($name)) {
        $this->build_domain_details(get_object_vars($details[$key]), $options);
      }
      else {
        $options[$key] = $name;
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
    $csdomain = new AWSCloudSearchDomain($this->options, $index->machine_name);
    if ($csdomain->is_active()) {
      $csdomain->update_indexes($index);
      $msg = t('CloudSearch indexes were updated. It can take up to 10 minutes for the indexes to be processed. During this time no items can be added to CloudSearch', information);
      drupal_set_message($msg, 'error');
    } 
    else{
      $msg = t('Unable to add an index as CloudSearch is not currently in an active state.');
      drupal_set_message($msg, 'error');
      watchdog('search_api_cloudwatch', check_plain($msg), NULL, WATCHDOG_WARNING);
    }
    
  }

  /**
   * Notify the server that the indexed field settings for the index have
   * changed.
   * If any user action is necessary as a result of this, the method should
   * use drupal_set_message() to notify the user.
   *
   * @param SearchApiIndex $index
   *   The updated index.
   *
   * @return
   *   TRUE, if this change affected the server in any way that forces it to
   *   re-index the content. FALSE otherwise.
   */
  public function fieldsUpdated(SearchApiIndex $index) {
    $csdomain = new AWSCloudSearchDomain($this->options, $index->machine_name);
    if ($csdomain->is_active() || 1==1) {
      $csdomain->update_indexes($index);
    } 
    else{
      $msg = t('Unable to add an index as CloudSearch is not currently in an active state.');
      drupal_set_message(check_plain($msg), 'error');
      watchdog('search_api_cloudwatch', check_plain($msg), NULL, WATCHDOG_WARNING);
    }
  }

  
  public function indexItems(SearchApiIndex $index, array $items) {
  	
    $csdomain = new AWSCloudSearchDomain($this->options, $index->machine_name);
    if ($csdomain->is_active()) {
      $csdomain->update_documents($items);
      return array_keys($items);
    } 
    else{
      $msg = t('Unable to add an index as CloudSearch is not currently in an active state.');
      drupal_set_message(check_plain($msg), 'error');
      watchdog('search_api_cloudwatch', check_plain($msg), NULL, WATCHDOG_WARNING);
    }    
    return array();
  }
  
  public function deleteItems($ids = 'all', SearchApiIndex $index = NULL) {
    if ($ids == 'all' && $index == NULL) {
      $ids = array();
      drupal_set_message(t('SearchAPI CloudSearch does not currently support the removal of all documents.'), 'warning');
    }
    $csdomain = new AWSCloudSearchDomain($this->options, $index->machine_name);
    if ($ids == 'all') {
      //get all ids for index type
      $ids = entity_load($index->item_type);
      $csdomain->remove_documents($ids);
    }
    else {
      $csdomain->remove_documents($ids);
    }
    
  }
  
  
  public function search(SearchApiQueryInterface $query) {
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
  
 
  
}

