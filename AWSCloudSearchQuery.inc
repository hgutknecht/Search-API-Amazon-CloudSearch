<?php

/**
 * @file
 * Encapsulates the logic required to perform an Amazon CloudSearch Query.
 * Takes a SeachAPI query and transforms into CloudSearch query.
 * It then submits this query to the CloudSearch search endpoint
 * and tranforms the results into the Search API results structure.
 */

class AWSCloudSearchQuery {

  protected $options;
  protected $indexMachineName;
  protected $query;
  protected $serverFieldOptions;
  protected $sharedFieldSiteName;
  protected $sharedFieldMode;
  /**
   * Constructor.
   */
  public function __construct($ops) {
    $this->options = $ops;
    $this->sharedFieldMode = !empty($ops['shared_field_mode']) ? $ops['shared_field_mode'] : false;
    $this->sharedFieldSiteName = !empty($ops['shared_field_site_name']) ? $ops['shared_field_site_name'] : '';

  }

  /**
   * Builds and submits the search query to CloudSearch.
   *
   * Sets up Facets and returns the Ids.
   */
  public function execute(SearchApiQueryInterface $search_api_query) {
    $query_options = $search_api_query->GetOptions();
    $this->indexMachineName = $search_api_query->getIndex()->machine_name;
    $this->query = $search_api_query;


    // Get the CloudSearch server specific field options for this index.
    $this->serverFieldOptions = $this->query->getIndex()->server()->options['index_options'][$this->indexMachineName]['fields'];

    /* Construct the CloudSearch querystring
    * Params are:
    *   q             - general keyword search, searches all searchable fields
    *   q_filter      - complex search query filters, search on multple fields
    *                   controled by parentheses
    *   return-fields - what fields to return
    *   facet         - what facets to calculate and return back
    *   start         - control the starting index of the return result
    *   size          - control number of hits to return back
    */

    // Sort out pager.
    $page_size = empty($query_options['limit']) ? 0 : $query_options['limit'];
    $page_size = $page_size > 0 ? "&size={$page_size}" : '';
    $currentpage = "&start={$query_options['offset']}";

    // Standard keyword search.
    $structured = false;
    $keys = $this->combineKeys($this->prepareKeys($search_api_query->getKeys()));
    $q = $keys == '' ? 'q=%' : "q={$keys}";

    // Always add a facet to limit searches to this site only
    if ($this->sharedFieldMode) {
      $site_facet = "&fq=search_api_document_index_machine_name:'$this->sharedFieldSiteName'";
    } else {
      $site_facet = "&fq=search_api_document_index_machine_name:'$this->indexMachineName'";
    }


    /* Return values, this is pretty simple in SearchAPI,
     * it only requires the item ID, we add text_relevance to
     * give Search API a score for this result for the default ordering
     */
    $rtn = "return=_score";

    // Add facets (if applicable).
    $facets = array();
    $f = '';
    if ($search_api_query->getOption('search_api_facets')) {
      foreach ($search_api_query->getOption('search_api_facets') as $key => $facet) {
        $facets[] = AWSCloudSearchDomain::encodeIndexName($this->indexMachineName, $key);
      }
      $f = "facet=";
      foreach ($facets as $facet) {
        $f .= "{$facet},";
      }
      $f = trim($f, ', ');
    }

    // Active filters (q_filter).  These filters will come from the
    // facets or from the View filters. If they exist, we need to
    // modify the query to be "structured"
    $filters = $search_api_query->getFilter();
    if (count($filters->getFilters()) > 0) {
      $structured = true;

      $q .= "(and (term {$keys}) ";
      $this->createFilterCondition($filters, 0, $q);
      $q .= ')';
    }


    /* Sort.  If no sort specified CloudSearch will default to
     * text-relevance field.
     */
    $sort = $search_api_query->getSort();
    $sort_expression = $this->options['index_options'][$this->indexMachineName]['sort_expression'];
    $sort_order = $this->options['index_options'][$this->indexMachineName]['sort_order'];
    $r = '';
    if ($sort_expression) {
      $r = '&sort=' . $sort_expression;
      $r .= $sort_order ? '+asc' : '+desc';
    }
    // TODO - Figure out how to handle search api driven sorting
    // else if ($sort) {
    //   $r = '&sort=';
    //   foreach ($sort as $field_name => $order) {
    //     $r .= $order == 'DESC' ? '+desc' : '+asc';
    //     if ($field_name == 'search_api_relevance') {
    //       $r .= '_score,';
    //     }
    //     elseif ($field_name == 'search_api_id') {
    //       $r .= 'id,';
    //     }
    //     else {
    //       $r .= AWSCloudSearchDomain::encodeIndexName($this->indexMachineName, 'sort_' . $field_name) . ',';
    //     }
    //   }
    //   $r = rtrim($r, ',');
    // }

    // Combine into CloudSearch query string.
    $query_params = "{$q}{$site_facet}{$f}{$r}&{$rtn}{$page_size}{$currentpage}";

    if ($structured) {
      $query_params .= '&q.parser=structured';
    }

    watchdog('search_api_amazon_cloudsearch', 'Executing search query with params: %params',
      array('%params' => $query_params), WATCHDOG_DEBUG);
    // Send query to CloudSearch endpoint.
    $response = json_decode($this->submitSearch($query_params));

    // Check response and report if in error.
    if ($response == NULL) {
      drupal_set_message(check_plain(t('Critial failure when calling CloudSearch')), 'error');
      watchdog('aws_cloudsearch', 'Critial failure when calling CloudSearch. Request was: %query_string',
        array('%query_string' => $query_params), WATCHDOG_CRITICAL, $link = NULL);

      return array();
    }
    elseif (isset($response->error)) {
      drupal_set_message(check_plain(t('CloudSearch returned an error:') . $response->error->message), 'error');
      watchdog('aws_cloudsearch', 'CloudSearch returned an error. Error was %error_message | Request was: %query_string',
        array('%query_string' => $query_params, '%error_message' => $response->error->message), WATCHDOG_CRITICAL, $link = NULL);

      return array();
    }

    /* CloudSearch request was successfull so build up the results
     * array and hand back to SearchAPI.
     */
    $results = array();
    $results['results'] = array();
    $results['result count'] = $response->hits->found;
    foreach ($response->hits->hit as $hit) {
      $id = end(explode('_', $hit->id));
      $results['results'][$id] = array(
        'id' => $id,
        'score' => $hit->fields->_score,
      );
    }

    // Facets.
    if (isset($response->facets)) {
      $rtn_facets = array();
      foreach ($response->facets as $key => $facet) {
        if (isset($facet->constraints)) {
          $terms = array();
          foreach ($facet->constraints as $term) {
            $terms[] = array(
              'count' => $term->count,
              'filter' => "\"{$term->value}\"",
            );
          }
          $rtn_facets[AWSCloudSearchDomain::decodeIndexName($this->indexMachineName, $key)] = $terms;
        }
      }
      $results['search_api_facets'] = $rtn_facets;
    }
    return $results;
  }

  /**
   * Helper - creates a CloudSearch filter (q_filter) string.
   *
   * Given SearchAPI filter object. CloudSearch queries
   * use parentheses to group searches and the conjunction
   * is put at the start of each group
   *
   * Example:  (AND field_1 field_2 (OR field_3 field 4))
   */
  protected function createFilterCondition(SearchApiQueryFilterInterface $filter, $depth, &$q_filter = '') {
    $q_filter .= '(' . drupal_strtolower($filter->GetConjunction()) . ' ';
    foreach ($filter->getFilters() as $f) {
      if (is_array($f)) {
        $val = $this->getFinalFieldFilterValue($f);
        $field_type = $this->serverFieldOptions[$f[0]]['index_type'];
        $fname = AWSCloudSearchDomain::encodeIndexName($this->indexMachineName, $f[0]);
        // Setup condition based on operand.
        switch ($f[2]) {
          case "=":
            $q_filter .= " {$fname}:{$val} ";
            break;

          case "<>":
            $q_filter .= " (not {$fname}:{$val}) ";
            break;

          case ">=":
            /* Field will need to be changed to the sort/ranged field
             * name if the standard field is not of type int. Applies
             * to all ranged queries.
             */
            $fname = $field_type != 'uint' ? $this->getSortFieldName($fname) : $fname;
            $q_filter .= " {$fname}:{$f[1]}.. ";
            break;

          case "<=":
            $fname = $field_type != 'uint' ? $this->getSortFieldName($fname) : $fname;
            $q_filter .= " {$fname}:0..{$f[1]} ";
            break;

          case ">":
            /* CloudSearch range filters are always inclusive so change value
             * to compensate.
             */
            $fname = $field_type != 'uint' ? $this->getSortFieldName($fname) : $fname;
            $val = $f[1] + 1;
            $q_filter .= " {$fname}:{$val}.. ";
            break;

          case "<":
            /* CloudSearch range filters are always inclusive so change value
             * to compensate.
             */
            $fname = $field_type != 'uint' ? $this->getSortFieldName($fname) : $fname;
            $val = $f[1] > 0 ? $f[1] - 1 : 0;
            $q_filter .= " {$fname}:0..{$val} ";
            break;

        }
      }
    }
    foreach ($filter->getFilters() as $f) {
      if (is_object($f)) {
        $c = $this->createFilterCondition($f, $depth, $q_filter);
      }
    }
    // Trailing space required.
    $q_filter .= ') ';
  }

  /**
   * Helper method for getting the filter field ready for AWS.
   *
   * This is need to ensure uint values are not wrapped in quotes
   * otherwise AWS will fail
   */
  protected function getFinalFieldFilterValue($field) {
    switch ($this->serverFieldOptions[$field[0]]['index_type']) {
      case 'literal':
        return "'$field[1]'";

      case 'text':
        return "'$field[1]'";

      case 'uint':
        return $field[1];
    }
  }

  /**
   * Converts a normal encoded field name into it's sort field.
   */
  protected function getSortFieldName($field_name) {
    return str_replace($this->indexMachineName, $this->indexMachineName . '_sort', $field_name);
  }

  /**
   * Helper method for removing unnecessary nested expressions from keys.
   *
   * Note: Taken from search_api_db.
   */
  protected function prepareKeys($keys) {
    if (is_scalar($keys)) {
      $keys = $this->splitKeys($keys);
      return is_array($keys) ? $this->eliminateDuplicates($keys) : $keys;
    }
    elseif (!$keys) {
      return NULL;
    }
    $keys = $this->eliminateDuplicates($this->splitKeys($keys));
    $conj = $keys['#conjunction'];
    $neg = !empty($keys['#negation']);
    foreach ($keys as $i => &$nested) {
      if (is_array($nested)) {
        $nested = $this->prepareKeys($nested);
        if ($neg == !empty($nested['#negation'])) {
          if ($nested['#conjunction'] == $conj) {
            unset($nested['#conjunction'], $nested['#negation']);
            foreach ($nested as $renested) {
              $keys[] = $renested;
            }
            unset($keys[$i]);
          }
        }
      }
    }
    $keys = array_filter($keys);
    if (($count = count($keys)) <= 2) {
      if ($count < 2 || isset($keys['#negation'])) {
        $keys = NULL;
      }
      else {
        unset($keys['#conjunction']);
        $keys = array_shift($keys);
      }
    }
    return $keys;
  }

  /**
   * Helper method for splitting keys.
   *
   * Note: Taken from search_api_db
   */
  protected function splitKeys($keys) {
    if (is_scalar($keys)) {
      $proc = drupal_strtolower(trim($keys));
      if (is_numeric($proc)) {
        return ltrim($proc, '-0');
      }

      $words = preg_split('/[^\p{L}\p{N}]+/u', $proc, -1, PREG_SPLIT_NO_EMPTY);
      if (count($words) > 1) {
        $proc = $this->splitKeys($words);
        $proc['#conjunction'] = 'AND';
      }
      return $proc;
    }
    foreach ($keys as $i => $key) {
      if (element_child($i)) {
        $keys[$i] = $this->splitKeys($key);
      }
    }
    return array_filter($keys);
  }

  /**
   * Helper method for combining keys to search query.
   */
  protected function combineKeys($keys) {
    if (is_scalar($keys)) {
      return $keys;
    }

    $op = ' ';
    if (isset($keys['#conjunction'])) {
      switch ($keys['#conjunction']) {
        case 'AND':
          $op = '+';
          break;

        case 'OR':
          $op = '|';
          break;

        default:
          $op = ' ';
          break;
      }
      unset($keys['#conjunction']);
    }

    return implode($op, $keys);
  }

  /**
   * Helper method for eliminating duplicates from the search keys.
   *
   * Note: Taken from search_api_db
   */
  protected function eliminateDuplicates($keys, &$words = array()) {
    foreach ($keys as $i => $word) {
      if (!element_child($i)) {
        continue;
      }
      if (is_scalar($word)) {
        if (isset($words[$word])) {
          unset($keys[$i]);
        }
        else {
          $words[$word] = true;
        }
      }
      else {
        $keys[$i] = $this->eliminateDuplicates($word, $words);
      }
    }
    return $keys;
  }


  /**
   * Performs the actual cURL request to the CloudSearch search endpoint.
   */
  protected function submitSearch($query_params) {
    $url = 'http://' . $this->options['search_endpoint'] . '/' . $this->options['cloudsearch_api_version'] . '/search?' . $query_params;
    // Only need to encode spaces.
    $url = str_replace(' ', '%20', $url);

    if ($this->options['show_debug']) {
      $msg1 = t('CLOUDSEARCH DEBUG SEARCH URL');
      $msg2 = t('This is the URL that was constructed in order to perform a CloudSearch request:');
      drupal_set_message(filter_xss("<strong>{$msg1}</strong><br/>{$msg2}<br/>") . l($url, $url), 'warning');
    }

    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_HEADER, 0);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    $response = curl_exec($ch);
    curl_close($ch);

    if ($this->options['show_debug']) {
      $msg1 = t('CLOUDSEARCH DEBUG SEARCH RESPONSE');
      $msg2 = t('This is the JSON response from CloudSearch given the URL above:');
      drupal_set_message(filter_xss("<strong>{$msg1}</strong><br/>{$msg2}<br/>") . $response, 'warning');
    }

    return $response;
  }

}
